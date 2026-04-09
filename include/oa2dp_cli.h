/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * oa2dp_cli.h - Headless command-line entry point.
 *
 * The CLI mode lets the user script Bluetooth actions from
 * Task Scheduler, login scripts, or hotkeys without ever opening the
 * GUI window.  Supported commands:
 *
 *   --reconnect <addr>        Cycle AudioSink for the given device
 *   --disable-hfp <addr>      Disable Handsfree on the given device
 *   --enable-a2dp <addr>      Enable AudioSink on the given device
 *   --help                    Print usage and exit
 *
 * <addr> is a Bluetooth address in the form XX:XX:XX:XX:XX:XX.
 */

#ifndef OA2DP_CLI_H
#define OA2DP_CLI_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Returns 1 if argv contains a recognised CLI command (regardless of
 * whether it was actually well-formed).  Used by wWinMain to decide
 * whether to skip GUI startup.
 */
int oa2dp_cli_is_cli_invocation(int argc, wchar_t **argv);

/*
 * Run the CLI command and return a process exit code.
 *   0 = success
 *   1 = action failed
 *   2 = bad usage
 * Attaches to the parent console (if any) so the user sees output.
 */
int oa2dp_cli_run(int argc, wchar_t **argv);

#ifdef __cplusplus
}
#endif

#endif /* OA2DP_CLI_H */
