# Deploying the edges

There is nothing to deploy yet, and this page says what has to exist first rather than
carrying a `Containerfile` that cannot build one.

`transport/` holds working picoquic code. `ingest/` and `gateway/` hold a contract and no
code, and neither has a `main`. The transport was called from the zone plane, and the two
processes that will call it here do not exist.

A build file that cannot build is worse than none, because CI then reports a failure that
says nothing about the code.

## What an edge deployment has to get right

**An edge is the only thing here with a public port.** A plane has none. So this is the
one app in the fabric that takes an IP address and a certificate.

- **A dedicated IPv4.** Fly's free allowance has none, and QUIC is UDP. Without one,
  external reachability cannot be proved from outside the machine, which is exactly what
  an edge exists to do.
- **A TLS certificate.** picoquic takes file paths and not PEM buffers, so a deployment
  that keeps the PEM in a secret writes it to disk first. `../TRANSPORT.md` records that,
  and the SELinux mount note beside it.
- **The bus, in the same machine.** iceoryx2 is shared memory: `/dev/shm` for the segments
  and `/tmp/iceoryx2` for the service registry. Two Fly machines share neither. So an edge
  and the plane it feeds run in one machine, as two processes in one image. A separate
  Fly app for each is the obvious shape and it does not work.

## The order

1. Give `ingest/` and `gateway/` a `main` each, over `thirdparty/harness`.
2. Build one image with both edges and the plane they feed, because the bus binds them to
   one machine.
3. Wire a certificate, and prove it with Firefox. The client is a browser, so a command
   line client proves the transport and not the product.
