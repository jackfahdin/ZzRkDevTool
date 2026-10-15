#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# 构建前与发布前共用校验，防止标签版本和安装包内版本不一致。
set -euo pipefail
version=${1:?需要传入 CMake 项目版本号}
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo '无法读取唯一的三段项目版本号' >&2
    exit 1
fi
if [[ "${GITHUB_EVENT_NAME:-}" == push && "${GITHUB_REF_TYPE:-}" == tag ]]; then
    if [[ "${GITHUB_REF_NAME:-}" != "v$version" ]]; then
        echo "标签 ${GITHUB_REF_NAME:-} 必须与项目版本一致：v$version" >&2
        exit 1
    fi
fi
