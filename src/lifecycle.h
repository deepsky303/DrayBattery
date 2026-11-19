#pragma once
// Local lifecycle/error log and sparse UI heartbeat; no device names/identifiers.
void LogDrayLifecycle(const wchar_t* event);
void LogDrayLifecycleCode(const wchar_t* event, unsigned long code);
void SetDrayPollStage(const wchar_t* stage);
const wchar_t* DrayPollStage();
