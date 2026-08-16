# Why the packet path is C++

Moved out of `README.md`, which is capped at 40 lines. Unchanged.

## The packet path is C++

The transport decodes every datagram, so the language is a packet-rate decision. Measured on one machine against a bar of 15 M snapshots per second per core:

| | rate |
| --- | --- |
| C++, `memcpy` decode | **841.51 M/s**, 56 times the bar |
| a scripting runtime | 5.70 M/s, 2.6 times under |

One crossing into a scripting runtime costs 117.8 ns and the whole per-packet budget is 66.7 ns. Policy that changes often lives in [`interactor-janet`](https://github.com/v-sekai-multiplayer-fabric/interactor-janet), an interactor on the bus that never appears in this path.
