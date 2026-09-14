# 版本标签与 Windows CI

[文档索引](README.md) · [项目首页](../README.md)

发布流程由两个独立工作流组成：版本工作流创建带注释的标签，并显式触发该标签上的 Windows CI；
构建和测试成功后，独立任务发布 GitHub Release。
逻辑直接写在 YAML 中，不依赖额外脚本。
内联 Python 使用标准库 tomllib 解析版本，PowerShell 负责 Git、工作流触发和工具安装。

## 版本标签

[version-tag.yml](../.github/workflows/version-tag.yml) 在推送到 main 且修改 mcpp.toml 时运行。
它按 TOML 解析推送前后提交中的 `[package].version`，一次推送包含多个提交时比较整体端点。
例如版本从 0.1.0 变成 0.2.0，会在推送的最终提交创建带注释标签 v0.2.0，
注释为 `mcr 0.2.0`，标签创建者为 GitHub Actions 机器人。

仅改依赖、描述、格式或注释而版本不变时，不创建标签。
首次创建分支或清单只建立比较基准，不打标签。
已有标签不会覆盖；若版本已标记在其他提交上，工作流失败，需使用未占用版本。
重试同一次推送时复用已有标签（包括早期的轻量标签），再次触发 Windows CI。
工作流不提交文件或自动递增版本。

支持在默认分支手动运行，比较所选提交及其第一父提交，版本未变仍不打标签。
多提交推送应重跑原工作流，以保留原始比较端点。
同一提交的并发尝试串行执行，不把不同版本变更合为一组。

## Windows 构建和测试

创建标签后，版本工作流使用内置令牌执行
`gh workflow run windows-ci.yml --ref <tag>`。
原因是 GITHUB_TOKEN 发起的推送不会启动新的 push 工作流，
但可触发 workflow_dispatch，见 [GitHub 触发规则](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/trigger-a-workflow)。

[windows-ci.yml](../.github/workflows/windows-ci.yml) 接受 workflow_dispatch 和用户推送的 v* 标签，
也可从 Actions 页面手动运行。普通分支推送和拉取请求不触发 Windows 测试；
同一引用的重复运行会取消较早、尚未完成的运行。

两个工作流均使用 windows-2025。测试任务检出所选引用，检查标签与包版本一致，
准备 MSVC x64 环境，然后安装经过固定校验和验证的 mcpp 官方 Windows 版本
`2026.9.11.1` 到运行器临时目录，安装并选择 `llvm@22.1.8`，
依次执行 `mcpp build` 和 `mcpp test`。
托管镜像提供 LLVM MSVC 目标需要的 Visual Studio 和 Windows SDK。
升级 mcpp 时，必须同时更新 Install mcpp 步骤中的版本与校验和。

标签任务请求 contents: write 和 actions: write；发布任务请求 contents: write；
测试任务仅需要仓库只读权限。使用内置令牌，无需额外密钥，
仓库规则必须允许标签任务创建版本标签。

Windows 测试失败时保留标签，并将独立 Windows CI 标记失败；排查后可重跑。
版本工作流成功仅表示标签存在且已触发 Windows CI，不能据此判断测试通过。

## 发布与本地验证

release 任务仅在 v* 标签引用上、test 成功后运行。
它执行 `gh release create --verify-tag --generate-notes`，
以 `mcr <version>` 为标题创建发布。
标签必须已存在，发布操作不会额外创建标签。
0.2.0-rc.1 等预发布版本标记为预发布；重跑时保留已有 Release。

基于分支的手动运行、构建失败或测试失败均不发布。
GitHub 提供标签对应的源码归档，本模块库不附加可执行构建产物。
参数说明见 [GitHub CLI 发布文档](https://cli.github.com/manual/gh_release_create)。

在仓库根目录验证：

```sh
actionlint .github/workflows/version-tag.yml .github/workflows/windows-ci.yml
mcpp test
```
