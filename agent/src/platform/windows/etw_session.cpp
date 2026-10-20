#include "syspulse/windows_collectors.hpp"

#ifdef _WIN32

#include <initguid.h>
#include <mutex>
#include <thread>
#include <vector>

// Microsoft-Windows-Kernel-Process GUID: {22fb2cd6-0e7b-4226-a0f7-2d4c97d5ac73}
DEFINE_GUID(KernelProcessGuid,
    0x22fb2cd6, 0x0e7b, 0x4226, 0xa0, 0xf7, 0x2d, 0x4c, 0x97, 0xd5, 0xac, 0x73);

// Microsoft-Windows-Kernel-Memory GUID: {d1d93ef7-e1f2-4f45-9943-03d245fe6c00}
DEFINE_GUID(KernelMemoryGuid,
    0xd1d93ef7, 0xe1f2, 0x4f45, 0x99, 0x43, 0x03, 0xd2, 0x45, 0xfe, 0x6c, 0x00);

namespace syspulse {

static std::mutex g_etw_mutex;
static std::vector<EventRecord> g_buffered_events;

// Static callback invoked by Windows ETW runtime for every emitted event
static void WINAPI EtwEventRecordCallback(PEVENT_RECORD pEventRecord) {
    if (!pEventRecord) return;

    EventRecord rec;
    rec.timestamp_ns = static_cast<uint64_t>(pEventRecord->EventHeader.TimeStamp.QuadPart * 100);
    rec.pid = pEventRecord->EventHeader.ProcessId;

    if (InlineIsEqualGUID(pEventRecord->EventHeader.ProviderId, KernelProcessGuid)) {
        rec.provider_name = "Microsoft-Windows-Kernel-Process";
        // Opcode 1 = ProcessStart, Opcode 2 = ProcessStop
        if (pEventRecord->EventHeader.EventDescriptor.Opcode == 1) {
            rec.event_type = "ProcessStart";
        } else if (pEventRecord->EventHeader.EventDescriptor.Opcode == 2) {
            rec.event_type = "ProcessStop";
        } else {
            rec.event_type = "ProcessEvent";
        }
    } else if (InlineIsEqualGUID(pEventRecord->EventHeader.ProviderId, KernelMemoryGuid)) {
        rec.provider_name = "Microsoft-Windows-Kernel-Memory";
        rec.event_type = "MemoryEvent";
    } else {
        rec.provider_name = "UnknownProvider";
        rec.event_type = "GenericEvent";
    }

    // TDH metadata decoding: TdhGetEventInformation
    DWORD buffer_size = 0;
    ULONG status = TdhGetEventInformation(pEventRecord, 0, nullptr, nullptr, &buffer_size);
    if (status == ERROR_INSUFFICIENT_BUFFER && buffer_size > 0) {
        std::vector<BYTE> info_buf(buffer_size);
        PTRACE_EVENT_INFO pInfo = reinterpret_cast<PTRACE_EVENT_INFO>(info_buf.data());
        if (TdhGetEventInformation(pEventRecord, 0, nullptr, pInfo, &buffer_size) == ERROR_SUCCESS) {
            if (pInfo->TaskNameOffset != 0) {
                const wchar_t* task_name = reinterpret_cast<const wchar_t*>(info_buf.data() + pInfo->TaskNameOffset);
                char narrow_buf[128];
                int len = WideCharToMultiByte(CP_UTF8, 0, task_name, -1, narrow_buf, sizeof(narrow_buf), nullptr, nullptr);
                if (len > 0) {
                    rec.details = "Task: " + std::string(narrow_buf);
                }
            }
        }
    }

    std::lock_guard<std::mutex> lock(g_etw_mutex);
    g_buffered_events.push_back(std::move(rec));
}

EtwSession::EtwSession() = default;

EtwSession::~EtwSession() {
    stop();
}

Result<void, std::string> EtwSession::start(const std::wstring& session_name) {
    session_name_ = session_name;

    // Allocate EVENT_TRACE_PROPERTIES structure with trailing session name buffer
    size_t prop_size = sizeof(EVENT_TRACE_PROPERTIES) + (session_name_.size() + 1) * sizeof(wchar_t) + 256;
    std::vector<BYTE> prop_buffer(prop_size, 0);
    PEVENT_TRACE_PROPERTIES pProps = reinterpret_cast<PEVENT_TRACE_PROPERTIES>(prop_buffer.data());

    pProps->Wnode.BufferSize = static_cast<ULONG>(prop_size);
    pProps->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    pProps->Wnode.ClientContext = 1; // QueryPerformanceCounter resolution
    pProps->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    pProps->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);

    // Stop previous orphaned session if exists
    ControlTraceW(0, session_name_.c_str(), pProps, EVENT_TRACE_CONTROL_STOP);

    // 1. StartTraceW: Registers and starts a kernel ETW logging session
    ULONG status = StartTraceW(&session_handle_, session_name_.c_str(), pProps);
    if (status != ERROR_SUCCESS) {
        return Error<void>("StartTraceW failed with error code: " + std::to_string(status) + 
                           " (Requires Administrator / Performance Log Users privilege)");
    }

    // 2. EnableTraceEx2: Enables Microsoft-Windows-Kernel-Process provider with default parameters
    status = EnableTraceEx2(session_handle_, &KernelProcessGuid, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                            TRACE_LEVEL_INFORMATION, 0x10, 0, 0, nullptr);
    if (status != ERROR_SUCCESS) {
        stop();
        return Error<void>("EnableTraceEx2 failed for KernelProcessGuid: " + std::to_string(status));
    }

    // 3. OpenTraceW: Prepares real-time consumer handle
    EVENT_TRACE_LOGFILEW log_file{};
    log_file.LoggerName = const_cast<wchar_t*>(session_name_.c_str());
    log_file.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    log_file.EventRecordCallback = EtwEventRecordCallback;

    trace_handle_ = OpenTraceW(&log_file);
    if (trace_handle_ == INVALID_PROCESSTRACE_HANDLE) {
        stop();
        return Error<void>("OpenTraceW failed to open real-time trace handle");
    }

    running_ = true;

    // Pump events in dedicated background worker thread
    std::thread([this]() {
        // ProcessTrace blocks until CloseTrace is called on trace_handle_
        ProcessTrace(&trace_handle_, 1, nullptr, nullptr);
    }).detach();

    return Ok();
}

void EtwSession::stop() {
    if (!running_) return;
    running_ = false;

    if (trace_handle_ != INVALID_PROCESSTRACE_HANDLE) {
        CloseTrace(trace_handle_);
        trace_handle_ = INVALID_PROCESSTRACE_HANDLE;
    }

    if (session_handle_ != 0) {
        size_t prop_size = sizeof(EVENT_TRACE_PROPERTIES) + 512;
        std::vector<BYTE> prop_buffer(prop_size, 0);
        PEVENT_TRACE_PROPERTIES pProps = reinterpret_cast<PEVENT_TRACE_PROPERTIES>(prop_buffer.data());
        pProps->Wnode.BufferSize = static_cast<ULONG>(prop_size);
        ControlTraceW(session_handle_, nullptr, pProps, EVENT_TRACE_CONTROL_STOP);
        session_handle_ = 0;
    }
}

std::vector<EventRecord> EtwSession::drain_events() {
    std::lock_guard<std::mutex> lock(g_etw_mutex);
    std::vector<EventRecord> drained = std::move(g_buffered_events);
    g_buffered_events.clear();
    return drained;
}

} // namespace syspulse

#endif // _WIN32
