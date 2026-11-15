# squashfs-tools for mk/rgds-image.sh: ROCKNIX's SYSTEM is unpacked, trimmed
# and repacked as root inside the container, so its owners and modes survive.
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends squashfs-tools zstd \
    && rm -rf /var/lib/apt/lists/*
