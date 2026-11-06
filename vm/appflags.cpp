// Set BEOS:APP_FLAGS on an app (here: a shell-script launcher) so the roster
// passes opened files as argv instead of a B_REFS_RECEIVED message.
#include <AppFileInfo.h>
#include <File.h>
#include <Roster.h>
#include <stdio.h>
int main(int argc, char** argv) {
	BFile file(argv[1], B_READ_WRITE);
	BAppFileInfo info(&file);
	status_t s = info.SetAppFlags(B_MULTIPLE_LAUNCH | B_ARGV_ONLY);
	uint32 flags = 0;
	info.GetAppFlags(&flags);
	printf("%s: %s, flags=0x%x\n", argv[1], strerror(s), (unsigned)flags);
	return s != B_OK;
}
