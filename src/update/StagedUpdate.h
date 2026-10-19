#pragma once

// Bootstrap for a staged update, called from main() before QApplication
// exists. When UpdateChecker has staged a version newer than this binary under
// <AppData>/updates, this hands the process over to it and returns only when
// there is nothing to hand over or the hand-over failed - the running binary
// then simply carries on as itself.
//
// StagedUpdate.cpp is the Linux side (execv into the unpacked AppImage),
// StagedUpdate_win.cpp the Windows one (a child process this one waits for).
// Each also holds UpdateChecker's platform members: which release asset to
// take, how to unpack it, where the staged exe lives, how to replace a file,
// and how to restart.
void execStagedUpdate(int argc, char *argv[]);
