#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

# 本地从 .gitmodules 中的 GitCode 地址获取；GitHub CI 使用对应的公开镜像。
# insteadOf 按 URL 前缀匹配，HTTPS 域名后是 /，SSH 简写的主机后才是 :。
# 按仓库明确映射：spdlog 对应 gabime，而非 jackfahdin，不能只替换域名。
# -c 仅影响当前命令及子进程，不向本地或全局 Git 配置写入转换规则。
git submodule sync --recursive
git -c url.https://github.com/.insteadOf=git@github.com: \
    -c url.https://github.com/jackfahdin/ZzPureTools.git.insteadOf=https://gitcode.com/jackfahdin/ZzPureTools.git \
    -c url.https://github.com/jackfahdin/ZzPureTools.git.insteadOf=git@gitcode.com:jackfahdin/ZzPureTools.git \
    -c url.https://github.com/jackfahdin/Zzlibusb.git.insteadOf=https://gitcode.com/jackfahdin/Zzlibusb.git \
    -c url.https://github.com/jackfahdin/Zzlibusb.git.insteadOf=git@gitcode.com:jackfahdin/Zzlibusb.git \
    -c url.https://github.com/gabime/spdlog.git.insteadOf=https://gitcode.com/jackfahdin/spdlog.git \
    -c url.https://github.com/gabime/spdlog.git.insteadOf=git@gitcode.com:jackfahdin/spdlog.git \
    submodule update --init --recursive --depth 1
