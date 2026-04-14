// keep_client_ref.c
// Keep a reference to dav1dplay_main so the linker pulls Client_program
// into the final binary when the dav1d interface is enabled.

extern int dav1dplay_main(int argc, char *argv[]);

__attribute__((used)) static const void *keep_dav1dplay_main_ptr = (const void *)&dav1dplay_main;
