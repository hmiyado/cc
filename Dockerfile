FROM node:24-slim

RUN npm install -g npm@latest && \
    echo "min-release-age=14" >> /root/.npmrc

RUN apt-get update && apt-get install -y \
    git curl unzip ca-certificates jq \
    && curl -fsSL https://bun.sh/install | bash \
    && curl -fsSL https://cli.github.com/packages/githubcli-archive-keyring.gpg \
       | dd of=/usr/share/keyrings/githubcli-archive-keyring.gpg \
    && echo "deb [signed-by=/usr/share/keyrings/githubcli-archive-keyring.gpg] \
       https://cli.github.com/packages stable main" \
       > /etc/apt/sources.list.d/github-cli.list \
    && apt-get update && apt-get install -y gh \
    && rm -rf /var/lib/apt/lists/*

ENV PATH="/root/.bun/bin:$PATH"

RUN curl -fsSL https://claude.ai/install.sh | bash \
    && cp -L /root/.local/bin/claude /usr/local/bin/claude \
    && chmod 755 /usr/local/bin/claude

RUN useradd -m -s /bin/bash claude
USER claude
ENV HOME=/home/claude
ENV PATH="/home/claude/.local/bin:$PATH"

WORKDIR /workspace

COPY --chown=claude:claude entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

ENTRYPOINT ["/entrypoint.sh"]