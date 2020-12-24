
#include "dirs.h"

#include <pwd.h>
#include <unistd.h>
#include <sys/types.h>

const char* getHomeDir() {
	struct passwd *pw = getpwuid(getuid());
	const char *homedir = pw->pw_dir;
	return homedir;
 }
