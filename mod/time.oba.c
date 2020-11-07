// Generated automatically from mod/time.oba. Do not edit.
const char* timeModSource =

    "// Now returns the time in seconds since the program started running.\n"
    "fn now {\n"
    "return __native_now()\n"
    "}\n"
    "\n"
    "// Sleep makes the calling thread sleep until seconds have elapsed.\n"
    "fn sleep seconds {\n"
    "__native_sleep(seconds)\n"
    "return None()\n"
    "}\n"
    "\n";