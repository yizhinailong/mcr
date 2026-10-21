# 版本标签与 Windows/Linux CI

[文档索引](README.md) · [项目首页](../README.md)

Windows CI 和 Linux CI 是两个独立工作流，在分支推送和 PR 时分别运行，也支持手动触发。
发布流程由版本标签工作流和 Release 工作流负责：前者创建带注释的标签，
后者复用两个平台的 CI，待构建和测试均成功后发布 GitHub Release 并附加打包产物。
逻辑直接写在 YAML 中，不依赖额外脚本。
内联 Python 使用标准库 tomllib 解析版本，PowerShell 负责 Git 和工作流触发，
工具安装分别使用 PowerShell（Windows）和 Bash（Linux）。

## 版本标签

[version-tag.yml](../.github/workflows/version-tag.yml) 在推送到 main 且修改 mcpp.toml 时运行。
它按 TOML 解析推送前后提交中的 `[package].version`，一次推送包含多个提交时比较整体端点。
例如版本从 0.1.0 变成 0.2.0，会在推送的最终提交创建带注释标签 v0.2.0，
注释为 `mcr 0.2.0`，标签创建者为 GitHub Actions 机器人。

仅改依赖、描述、格式或注释而版本不变时，不创建标签。
首次创建分支或清单只建立比较基准，不打标签。
已有标签不会覆盖；若版本已标记在其他提交上，工作流失败，需使用未占用版本。
重试同一次推送时复用已有标签（包括早期的轻量标签），再次触发 Release 工作流中的 Windows/Linux CI。
工作流不提交文件或自动递增版本。

支持在默认分支手动运行，比较所选提交及其第一父提交，版本未变仍不打标签。
多提交推送应重跑原工作流，以保留原始比较端点。
同一提交的并发尝试串行执行，不把不同版本变更合为一组。

## Windows/Linux 构建和测试

创建标签后，版本工作流使用内置令牌执行
`gh workflow run release.yml --ref <tag>`。
原因是 GITHUB_TOKEN 发起的推送不会启动新的 push 工作流，
但可触发 workflow_dispatch，见 [GitHub 触发规则](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/trigger-a-workflow)。

[windows-ci.yml](../.github/workflows/windows-ci.yml) 和
[linux-ci.yml](../.github/workflows/linux-ci.yml) 分别显示为 Windows CI 和 Linux CI，触发方式相同：

- 向任意分支推送提交，无需修改 mcpp.toml 或版本号。
- 创建、更新或重新打开 PR。
- 从 Actions 页面手动运行所选工作流。
- 通过 workflow_call，由 Release 工作流调用以验证版本标签。

分支删除时跳过测试。两个平台独立运行，一个平台失败不影响另一个平台完成验证。
同一测试工作流、同一引用的重复运行会取消较早、尚未完成的运行；
Release 对同一引用串行执行且不取消正在发布的运行。
两个平台和 Release 使用不同的并发组，避免相互取消。

版本标签工作流使用 windows-2025。各平台 CI 的运行环境如下：

| 平台 | 运行器 | Shell | TLS 后端 |
| --- | --- | --- | --- |
| Windows x64 | `windows-2025` | PowerShell | Schannel |
| Linux x64 | `ubuntu-24.04` | Bash | OpenSSL |

每个测试任务检出触发事件对应的提交 SHA；PR 验证 GitHub 生成的合并提交。
在标签引用上运行时检查标签与包版本一致。随后安装经过固定 SHA-256 校验和验证的
mcpp 官方版本 `2026.10.19.1` 到运行器临时目录，保留安装包内置的 registry/bin/xlings，
并设置隔离的 MCPP_HOME（Windows 使用 xlings.exe）。随后安装并选择 `llvm@23.1.3`，
依次执行 `mcpp build` 和 `mcpp test`。分支推送和 PR 只运行测试，不打包。
库和测试启用 `-Wall -Wextra -Werror`。Linux 使用 `dev` 与 `sanitize` 两个独立矩阵任务，
后者通过 `--profile sanitize` 启用 ASan/UBSan，报告内存泄漏并在未定义行为发生时失败。
Linux 两个任务都要求本地 HTTPS 与双向 TLS 的成功标记。
Windows 和 Linux 的普通任务随后进入 `example/` 构建全部目标，
根据清单定位完整的 bin 目录，用已配置的 Python 执行 `scripts/verify.py --bin-dir <目录>`，
验证全部 46 个请求示例。sanitizer 任务不重复构建示例或上传归档。
标签引用（即版本变化创建的标签）在测试通过后追加 `mcpp pack --release`，
把接口和预构建产物打成 `mcr-<version>-windows-x64.zip` 或
`mcr-<version>-linux-x64.tar.gz`，并作为 Actions 产物上传。Linux 的 OpenSSL 依赖和编译定义由 mcpp.toml 提供。
Linux 使用工具链自带的 libc++ 23，提供代码所需的 `std::views::enumerate`，
其标准库支持情况见 [libc++ C++23 状态](https://libcxx.llvm.org/Status/Cxx23.html)。
Windows 任务额外准备 MSVC x64 环境，托管镜像提供 Visual Studio 和 Windows SDK。
升级 mcpp 时，必须同时更新两个平台安装步骤中的版本与对应校验和。
第三方 action 固定到提交 SHA，并在注释保留对应版本；更新时同时核对版本与提交。

标签任务请求 contents: write 和 actions: write；发布任务请求 contents: write；
测试任务仅需要仓库只读权限。使用内置令牌，无需额外密钥，
仓库规则必须允许标签任务创建版本标签。

版本工作流成功仅表示标签存在且已触发 Release，不能据此判断测试通过。

## 发布与本地验证

[release.yml](../.github/workflows/release.yml) 接受用户推送的 v* 标签和 workflow_dispatch，
使用仓库内相对路径并行调用同一提交中的 Windows CI 和 Linux CI。
任一平台失败或被取消时不发布，标签保留，排查后可重跑 Release 工作流。
工作流复用规则见 [GitHub 文档](https://docs.github.com/en/actions/how-tos/reuse-automations/reuse-workflows)。

release 任务在 windows-2025 上运行，通过 `needs: [windows, linux]` 等待两个平台均成功，
且仅在 v* 标签引用上运行。它先下载两个平台上传的打包归档，
再执行 `gh release create --verify-tag --generate-notes`，
以 `mcr <version>` 为标题创建发布并附上这些归档。
标签必须已存在，发布操作不会额外创建标签。
0.2.0-rc.1 等预发布版本标记为预发布；重跑时保留已有 Release，
并用 `gh release upload --clobber` 更新同名归档。

独立运行 Windows CI 或 Linux CI、基于分支的手动运行、构建失败或测试失败均不发布，
也不会产生打包归档。GitHub 标签页同时提供对应的源码归档。
参数说明见 [GitHub CLI 发布文档](https://cli.github.com/manual/gh_release_create)。

在仓库根目录验证：

```sh
actionlint .github/workflows/version-tag.yml .github/workflows/windows-ci.yml .github/workflows/linux-ci.yml .github/workflows/release.yml
mcpp test
```
