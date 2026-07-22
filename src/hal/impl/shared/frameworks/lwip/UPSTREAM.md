# lwIP upstream

- Upstream: `https://github.com/lwip-tcpip/lwip`
- Commit: `77dcd25a72509eb83f72b033d219b1d40cd8eb95`
- Version: 2.2.1
- Selected by pico-sdk commit
  `8fcd44a1718337861214ba5499a8faceea2bfa1d` used by the pinned
  Arduino-Pico 5.4.0 baseline.
- Scope: the unchanged upstream `src/` tree and its BSD-style `COPYING` file.

The STM32G474 CYW43 build uses an explicit source list from this snapshot. It
does not discover or link lwIP from the locally installed Arduino-Pico carrier.
The port configuration and JaszczurHAL lifecycle adapter live outside
`vendor/`.
