FROM mcr.microsoft.com/devcontainers/dotnet:10.0-noble

RUN apt-get update && apt-get install -y \
    curl \
    wget \
    git \
    && rm -rf /var/lib/apt/lists/*

# NuGet 패키지 및 설정 디렉토리 미리 생성 및 권한 설정
RUN mkdir -p /home/vscode/.nuget/packages \
    && chown -R vscode:vscode /home/vscode/.nuget

# 작업 디렉토리 설정
WORKDIR /workspace