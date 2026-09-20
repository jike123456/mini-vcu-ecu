#ifndef HOST_LOG_TASK_H
#define HOST_LOG_TASK_H
#define PID_KAOYATEACH_ENABLE 0
void Host_Log(const char *tag, const char *format, ...);
#define LOGWARN_T(...) Host_Log(__VA_ARGS__)
#define LOGINFO_T(...) Host_Log(__VA_ARGS__)
#endif
