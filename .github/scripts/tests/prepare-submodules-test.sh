#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

# 只替换网络下载入口，URL 转换由真实 Git 执行，避免用字符串断言代替行为验证。
git() {
    if [[ "$*" == 'submodule sync --recursive' ]]; then
        return
    fi
    local -a options=()
    while [[ "${1:-}" == -c ]]; do
        options+=("$1" "$2")
        shift 2
    done
    [[ "$*" == 'submodule update --init --recursive --depth 1' ]]
    local repository source actual expected
    for repository in ZzPureTools Zzlibusb spdlog; do
        expected="https://github.com/jackfahdin/$repository.git"
        if [[ "$repository" == spdlog ]]; then
            expected='https://github.com/gabime/spdlog.git'
        fi
        for source in "https://gitcode.com/jackfahdin/$repository.git" \
                      "git@gitcode.com:jackfahdin/$repository.git"; do
            actual=$(command git "${options[@]}" ls-remote --get-url "$source")
            if [[ "$actual" != "$expected" ]]; then
                echo "转换错误：$source -> $actual，期望 $expected" >&2
                return 1
            fi
            echo "通过：$source -> $actual"
        done
    done
    actual=$(command git "${options[@]}" ls-remote --get-url git@github.com:jackfahdin/ZzPureTools.git)
    [[ "$actual" == https://github.com/jackfahdin/ZzPureTools.git ]]
    # 不重定向未知 GitCode 仓库，防止递归依赖被静默替换到不对应的来源。
    source='https://gitcode.com/another/project.git'
    actual=$(command git "${options[@]}" ls-remote --get-url "$source")
    [[ "$actual" == "$source" ]]
}
export -f git
bash "$script_dir/prepare-submodules.sh"
