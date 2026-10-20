#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# 各平台只上传自己的附件；重跑可以替换同名附件，但不删除其他平台产物。
set -euo pipefail
shopt -s nullglob
: "${GH_REPO:?}"
: "${GITHUB_SHA:?}"
: "${GITHUB_REF_NAME:?}"
: "${GITHUB_REF_TYPE:?}"
: "${GITHUB_EVENT_NAME:?}"
: "${RELEASE_PLATFORM:?}"
: "${RELEASE_VERSION:?}"
: "${RELEASE_KIND:?}"
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
bash "$script_dir/validate-version-ref.sh" "$RELEASE_VERSION"
[[ "$GITHUB_EVENT_NAME" == push ]]
case "$RELEASE_PLATFORM" in linux|windows|macos) ;; *) exit 1 ;; esac
case "$RELEASE_KIND:$GITHUB_REF_TYPE:$GITHUB_REF_NAME" in
    continuous:branch:main|continuous:branch:master|stable:tag:"v$RELEASE_VERSION") ;;
    *) echo '发布来源不符：持续构建要求主分支 push，正式版要求匹配版本标签的 push。' >&2; exit 1 ;;
esac

# 只有持续构建要求仍是分支尖端；正式版绑定推送的版本标签，允许后续开发继续。
if [[ "$RELEASE_KIND" == continuous ]]; then
    branch_sha=$(gh api "repos/$GH_REPO/git/ref/heads/$GITHUB_REF_NAME" --jq .object.sha)
    if [[ "$branch_sha" != "$GITHUB_SHA" ]]; then
        echo '主分支已有更新提交，本次仅保留 Actions artifact，跳过发布。'
        exit 0
    fi
fi

# 不接受任意附件路径；版本和平台必须匹配当前调用者。
mkdir -p release-output
case "$RELEASE_PLATFORM" in
    linux)
        packages=(release-input/ZzRkDevTool-"$RELEASE_VERSION"-linux-*-portable.tar.gz
                  release-input/zzrkdevtool_"$RELEASE_VERSION"_*.deb)
        expected=2
        ;;
    windows)
        packages=(release-input/ZzRkDevTool-"$RELEASE_VERSION"-windows-*.tar.gz)
        expected=1
        ;;
    macos)
        packages=(release-input/ZzRkDevTool-"$RELEASE_VERSION"-macos-*.dmg)
        expected=1
        ;;
esac
if (( ${#packages[@]} != expected )); then
    echo "发布包数量不符：$RELEASE_PLATFORM，需要 $expected 个" >&2
    exit 1
fi

if [[ "$RELEASE_KIND" == stable ]]; then
    tag="$GITHUB_REF_NAME"
    title="ZzRkDevTool $RELEASE_VERSION"
    notes="ZzRkDevTool $RELEASE_VERSION，提交 $GITHUB_SHA。请在发布前补充更新说明并完成对应平台验证。"
    create_flags=(--draft)
    # 标签必须已由用户推送；CI 不创建或移动正式版本标签。
    gh api "repos/$GH_REPO/git/ref/tags/$tag" >/dev/null
    # commits API 解析轻量标签和附注标签，避免拿标签对象 SHA 与提交 SHA 比较。
    tag_sha=$(gh api "repos/$GH_REPO/commits/$tag" --jq .sha)
    if [[ "$tag_sha" != "$GITHUB_SHA" ]]; then
        echo "版本标签 $tag 已指向其他提交，拒绝上传本次构建。" >&2
        exit 1
    fi
else
    tag=continuous-build
    title='Continuous Build'
    notes="主分支 $GITHUB_REF_NAME 的持续构建，提交 $GITHUB_SHA。各平台附件在编译通过后分别更新，仅用于测试；正式发布请使用版本标签。"
    create_flags=(--prerelease --latest=false)
    # 镜像同步可能删除仅由 GitHub CI 创建的标签，已有 release 并不保证 ref 存在。
    # 只为持续构建补建标签；403 等错误直接失败，不能当作标签缺失处理。
    if ! tag_response=$(gh api "repos/$GH_REPO/git/ref/tags/$tag" 2>&1); then
        if [[ "$tag_response" != *'(HTTP 404)'* ]]; then
            printf '%s\n' "$tag_response" >&2
            exit 1
        fi
        if ! gh api --method POST "repos/$GH_REPO/git/refs" \
            -f ref="refs/tags/$tag" -f sha="$GITHUB_SHA" >/dev/null; then
            # 多个平台可能同时补建；只有确认标签已存在才接受创建冲突。
            gh api "repos/$GH_REPO/git/ref/tags/$tag" >/dev/null
        fi
    fi
fi

# 三个平台可能同时首次创建同一 release；创建失败后必须确认它确实已存在。
# 不用无条件 || true 掩盖权限、网络或 API 错误。
if ! gh release view "$tag" >/dev/null 2>&1; then
    # 正式版额外要求远端标签存在，避免标签被删除后由 release create 隐式重建。
    verify_flags=()
    if [[ "$RELEASE_KIND" == stable ]]; then verify_flags=(--verify-tag); fi
    if ! gh release create "$tag" --target "$GITHUB_SHA" --title "$title" \
        --notes "$notes" "${create_flags[@]}" "${verify_flags[@]}"; then
        gh release view "$tag" >/dev/null
    fi
fi
if [[ "$RELEASE_KIND" == stable ]]; then
    is_draft=$(gh release view "$tag" --json isDraft --jq .isDraft)
    if [[ "$is_draft" != true ]]; then
        echo "正式版 $tag 已发布，拒绝通过重跑 CI 修改已发布附件。" >&2
        exit 1
    fi
    # 正式版已有的标题、说明和草稿属性不变，避免覆盖手动填写的更新日志。
else
    # 下载 artifact、等待平台并发任务期间主分支可能变化，移动标签前再检查。
    branch_sha=$(gh api "repos/$GH_REPO/git/ref/heads/$GITHUB_REF_NAME" --jq .object.sha)
    if [[ "$branch_sha" != "$GITHUB_SHA" ]]; then
        echo '主分支已更新，跳过过期预发布。'
        exit 0
    fi
    # continuous-build 是唯一可移动标签；正式版本标签永不强制移动。
    gh api --method PATCH "repos/$GH_REPO/git/refs/tags/$tag" \
        -f sha="$GITHUB_SHA" -F force=true >/dev/null
    # 持续构建应公开为预发布，兼容迁移后残留的同名草稿；正式版仍保留草稿流程。
    gh release edit "$tag" --title "$title" --notes "$notes" --draft=false --prerelease --latest=false
fi

assets=()
for package in "${packages[@]}"; do
    name=$(basename "$package")
    if [[ "$RELEASE_KIND" == continuous ]]; then
        # 稳定文件名便于分享链接；DEB 内的软件版本仍保留实际版本号。
        name=${name/"$RELEASE_VERSION"/continuous}
    fi
    cp "$package" "release-output/$name"
    assets+=("release-output/$name")
done
gh release upload "$tag" "${assets[@]}" --clobber
