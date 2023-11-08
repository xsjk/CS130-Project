#include <syscall.h>

int main (int, char *[]);

#ifdef __cplusplus
extern "C" void
#else
void
#endif
_start (int argc, char *argv[])
{
  exit (main (argc, argv));
}
