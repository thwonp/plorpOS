/* adbd on the RG DS stays root (plorpos-rgds.11).
 *
 * Debian's adbd (android-tools-adbd 5.1.1) always drops to Android's "shell"
 * user, 2000, which on ROCKNIX is nobody: it then cannot open the gadget's
 * endpoints, and a shell that did work could not deploy anything. Preloaded
 * into adbd, this turns the drop into nothing, so adb is root as ssh is.
 */
#define _GNU_SOURCE
#include <grp.h>
#include <unistd.h>

int setuid(uid_t u) { (void)u; return 0; }
int setgid(gid_t g) { (void)g; return 0; }
int setgroups(size_t n, const gid_t *l) { (void)n; (void)l; return 0; }
