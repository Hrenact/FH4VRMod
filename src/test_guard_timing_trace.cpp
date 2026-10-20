#include "guard_timing_trace.h"
#include <cstring>
int main(){guard_timing::Recorder<2> r;
 SetLastError(1234);r.Record(1,18,123,62,122,4);if(GetLastError()!=1234)return 1;
 r.Record(3,0,125,62);r.Record(1,0,126,63);
 if(r.Count()!=2||r.Dropped()!=1)return 2;
 wchar_t path[128]{};swprintf_s(path,L"reports\\guard-trace-test-%lu.csv",GetCurrentProcessId());
 if(r.Save(path)!=S_OK)return 3;
 FILE*f=nullptr;if(_wfopen_s(&f,path,L"r")||!f)return 4;
 char line[512];unsigned lines=0;bool payload=false,overflow=false;
 while(fgets(line,sizeof(line),f)){++lines;if(strstr(line,",1,18,123,62,122,4"))payload=true;if(strstr(line,"dropped=1"))overflow=true;}
 fclose(f);DeleteFileW(path);if(lines!=4||!payload||!overflow)return 5;
 puts("PASS bounded guard trace, overflow reporting, LastError, deferred CSV payload");}
