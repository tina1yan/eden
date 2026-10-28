# Experimental audio shutdown fix

This is a personal, AI-assisted fork, not an official Eden release. The installed Windows fix and the validation below are based on Eden v0.2.1 (`58c1e20ee58efa3900ba616207d460886214480b`).

## Branches

- [Validated v0.2.1 code](https://github.com/tina1yan/eden/commit/fdb5f0b07fa0f04af5ebd9790e237ffc7edee858)
- [Master-based review branch](https://github.com/tina1yan/eden/tree/fix/audio-shutdown-queue), based on `26a03d2473787abf3223b2d0e65b291eee00c1e1`. The four affected files differ from the validated branch only by an existing upstream comment change. The complete master-based build has **not** been rebuilt or runtime-tested.

## Failure and fix

A symbolized, unmodified v0.2.1 Windows build reproduced a game-exit hang: the DSP thread blocked in `SinkStream::AppendBuffer -> SPSCQueue::EmplaceWait`, while renderer shutdown waited for DSP termination. The queue was full. During shutdown the audio callback emitted silence without consuming that queue.

The fix lets queue producers and playback-space waiters observe shutdown, including when device callbacks have stopped. Buffer metadata, samples and queue counts are published under the consumer's mutex. The shutdown callback no longer resets the count without dequeuing buffers. Renderer teardown and lifetime waits remain intact.

## Validation on v0.2.1

- Unmodified audio source with the new regression tests: 3 of 5 cases failed; 6 of 32 assertions failed.
- Patched audio: all 5 cases passed, 32 assertions.
- Patched audio plus common tests: all 40 cases passed, 3,851,421 assertions.
- Five live stop/close checks across three games passed. These included relaunching in the same process, pause/resume, normal stop, and closing the application while paused.
- Four checks used the executable directly without the recovery launcher. The installed-build check returned to the library without triggering its fallback.
- The user confirmed normal audible background music.

The full existing suite was not green: memory tracking assertions, a DeviceMemoryManager crash, input calibration assertions and CoreTiming assertions also reproduced with unchanged audio source in this Windows environment. This fix does not claim to resolve them. Other platforms and long gameplay sessions remain untested.

## Upstream status and disclosure

No upstream PR has been submitted or merged. The upstream project remains active and accepts contributions, subject to its [AI contribution policy](https://github.com/eden-emulator/mirror/blob/master/docs/policies/AI.md).

Investigation, implementation, regression tests and these notes were produced with OpenAI Codex assistance at the user's request. Human code review and sign-off have not been completed. Before upstream submission, a human contributor must review and test the master-based change, take responsibility for it, and author the final commit/PR text under the upstream requirements.

This repository contains source changes and tests only; no game files, console keys, firmware, saves or memory dumps are included in this patch.
