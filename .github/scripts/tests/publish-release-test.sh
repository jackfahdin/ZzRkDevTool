#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# 仅用导出的 Bash 函数模拟 gh；绝不连接 GitHub，也不需要真实 Token。
set -euo pipefail
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
test_root=$(mktemp -d /tmp/zzrk-release-test.XXXXXX)
trap 'rm -rf "$test_root"' EXIT

gh() {
    printf '%s\n' "$*" >> "$MOCK_LOG"
    case "$*" in
        'api repos/test/repo/git/ref/heads/main --jq .object.sha')
            [[ "$TEST_CASE" != api-error ]] || return 1
            if [[ "$TEST_CASE" == stale || "$TEST_CASE" == stable-stale ]]; then echo newer; else echo abc123; fi ;;
        'api repos/test/repo/git/ref/tags/v0.1.0')
            [[ "$TEST_CASE" != missing-tag ]] ;;
        'api repos/test/repo/commits/v0.1.0 --jq .sha')
            if [[ "$TEST_CASE" == collision ]]; then echo other; else echo abc123; fi ;;
        'release view v0.1.0 --json isDraft --jq .isDraft')
            if [[ "$TEST_CASE" == published ]]; then echo false; else echo true; fi ;;
        'release view continuous-build'|'release view v0.1.0')
            if [[ "$TEST_CASE" == race || "$TEST_CASE" == fresh ]]; then
                test -f "$MOCK_STATE"
            fi ;;
        'release create '*)
            touch "$MOCK_STATE"
            [[ "$TEST_CASE" != race ]] ;;
        'api --method POST '*|'api --method PATCH '*|'release edit '*|'release upload '*) ;;
        *) echo "未预期的 gh 调用：$*" >&2; return 1 ;;
    esac
}
export -f gh
export GH_REPO=test/repo GITHUB_SHA=abc123 GITHUB_REF_NAME=main
export GITHUB_REF_TYPE=branch GITHUB_EVENT_NAME=push
export RELEASE_VERSION=0.1.0 RELEASE_PLATFORM=linux

for scenario in continuous stable stable-stale stale collision published api-error missing race fresh missing-tag stable-branch continuous-tag mismatch invalid-tag manual; do
    (
        export TEST_CASE="$scenario"
        export MOCK_LOG="$test_root/$scenario.log" MOCK_STATE="$test_root/$scenario.state"
        touch "$MOCK_LOG"
        mkdir -p "$test_root/$scenario/release-input"
        cd "$test_root/$scenario"
        touch release-input/ZzRkDevTool-0.1.0-linux-x86_64-portable.tar.gz
        if [[ "$scenario" != missing ]]; then
            touch release-input/zzrkdevtool_0.1.0_amd64.deb
        fi
        export RELEASE_KIND=continuous
        case "$scenario" in
            stable|stable-stale|collision|published|fresh|missing-tag|stable-branch|mismatch|invalid-tag)
                export RELEASE_KIND=stable GITHUB_REF_TYPE=tag GITHUB_REF_NAME=v0.1.0 ;;
        esac
        case "$scenario" in
            stable-branch) export GITHUB_REF_TYPE=branch GITHUB_REF_NAME=main ;;
            continuous-tag) export GITHUB_REF_TYPE=tag GITHUB_REF_NAME=v0.1.0 ;;
            mismatch) export GITHUB_REF_NAME=v0.2.0 ;;
            invalid-tag) export GITHUB_REF_NAME=v0.1.0-rc1 ;;
            manual) export GITHUB_EVENT_NAME=workflow_dispatch ;;
        esac
        result=0
        bash "$script_dir/publish-release.sh" > output.log 2>&1 || result=$?
        case "$scenario" in
            collision|published|api-error|missing|missing-tag|stable-branch|continuous-tag|mismatch|invalid-tag|manual)
                test "$result" -ne 0
                ! grep -q '^release upload ' "$MOCK_LOG" ;;
            stale)
                test "$result" -eq 0
                ! grep -Eq '^release (create|edit|upload)|^api --method' "$MOCK_LOG" ;;
            continuous|race)
                test "$result" -eq 0
                grep -q '^api --method PATCH .*continuous-build' "$MOCK_LOG"
                grep -q '^release upload continuous-build .*ZzRkDevTool-continuous-linux-' "$MOCK_LOG"
                test -f release-output/zzrkdevtool_continuous_amd64.deb ;;
            stable|stable-stale|fresh)
                test "$result" -eq 0
                grep -q '^release upload v0.1.0 ' "$MOCK_LOG"
                ! grep -q '^release edit ' "$MOCK_LOG"
                ! grep -q '^api --method PATCH ' "$MOCK_LOG"
                ! grep -q '^api --method POST ' "$MOCK_LOG"
                ! grep -q '/git/ref/heads/' "$MOCK_LOG"
                test -f release-output/ZzRkDevTool-0.1.0-linux-x86_64-portable.tar.gz
                if [[ "$scenario" == fresh ]]; then
                    grep -q '^release create v0.1.0 .*--draft' "$MOCK_LOG"
                    grep -q '^release create v0.1.0 .*--verify-tag' "$MOCK_LOG"
                fi ;;
        esac
        echo "通过：$scenario"
    )
done

# 构建准备阶段使用同一校验器；错误标签必须在下载 Qt/编译之前失败。
for tag in v0.1.0 v0.2.0 v0.1.0-rc1 version-0.1.0; do
    result=0
    GITHUB_REF_TYPE=tag GITHUB_REF_NAME="$tag" bash "$script_dir/validate-version-ref.sh" 0.1.0 \
        > "$test_root/validate.log" 2>&1 || result=$?
    if [[ "$tag" == v0.1.0 ]]; then test "$result" -eq 0; else test "$result" -ne 0; fi
    echo "通过：构建前标签校验 $tag"
done
