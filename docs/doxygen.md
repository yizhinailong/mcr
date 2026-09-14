# Doxygen：生成 API 文档

[文档索引](README.md) · [项目首页](../README.md)

Doxygen 从源码注释生成类、函数、参数和返回值说明，并将 Markdown 使用指南纳入同一站点。
项目配置位于根目录的 `Doxyfile`，已使用 Doxygen 1.18.0 验证。
安装方法见 [Doxygen 官方安装说明](https://www.doxygen.nl/manual/install.html)。

## 生成与查看

从仓库根目录执行：

```sh
doxygen --version
doxygen Doxyfile
```

使用浏览器打开 `target/doxygen/html/index.html`。在 Windows PowerShell 中可以执行：

```powershell
Invoke-Item .\target\doxygen\html\index.html
```

修改源码注释或 Markdown 后，再次运行 `doxygen Doxyfile` 即可更新。
输出位于 Git 已忽略的 `target/` 下；`mcpp clean` 会一并移除这些生成文件。

## 项目配置

下面是影响本项目文档生成的主要配置，可在 Doxyfile 中搜索同名选项修改。
相对路径以运行 Doxygen 时的工作目录为准，因此应始终从仓库根目录运行。

| 配置 | 当前值 | 作用 |
| --- | --- | --- |
| `PROJECT_NAME` | `mcr` | 页面中的项目名称 |
| `PROJECT_BRIEF` | 项目的中文简介 | 页面顶部的项目说明 |
| `OUTPUT_DIRECTORY` | `target/doxygen` | 生成文件的存放目录 |
| `OUTPUT_LANGUAGE` | `Chinese` | 使用中文导航和内置标题 |
| `INPUT_ENCODING` | `UTF-8` | 源码与 Markdown 的字符编码 |
| `INPUT` | `src README.md docs example/README.md AGENTS.md` | 库源码、使用指南、示例说明与贡献规范 |
| `FILE_PATTERNS` | `*.cppm *.md` | 扫描的文件扩展名 |
| `RECURSIVE` | `YES` | 递归扫描输入目录 |
| `USE_MDFILE_AS_MAINPAGE` | `README.md` | 使用项目 README 作为首页 |
| `SOURCE_BROWSER` | `YES` | 生成可跳转的源码页面 |
| `GENERATE_HTML` | `YES` | 生成浏览器可查看的文档 |
| `GENERATE_LATEX` | `NO` | 不生成 LaTeX 文档 |
| `GENERATE_TREEVIEW` | `YES` | 显示树状导航 |
| `HAVE_DOT` | `NO` | 默认无需安装 Graphviz |

`OUTPUT_LANGUAGE` 不翻译注释正文，已有英文注释仍以英文显示。
新增使用指南应使用简体中文，并加入 [文档索引](README.md)。

查看与 Doxygen 默认配置不同的选项：

```sh
doxygen -x Doxyfile
```

完整选项含义见 [官方配置参考](https://www.doxygen.nl/manual/config.html)。

## C++23 模块与扫描范围

当前 Doxygen 原生识别 `.cppm`，无需额外设置 `EXTENSION_MAPPING`。
它直接解析模块源码和注释，无需先执行 `mcpp build`，也不需要生成 `import std;` 的编译产物。
`CLANG_ASSISTED_PARSING` 保持 `NO`，配置中不绑定本机编译器或依赖缓存路径。
生成文档不能代替编译和测试。

`INPUT` 显式列出库源码和说明文件，不扫描依赖缓存、构建目录或独立测试程序。
若需要把测试函数也纳入 API 文档，可在现有配置中增加：

```ini
INPUT += tests
FILE_PATTERNS += *.cpp *.hpp
```

## 文档警告

保留以下设置，用于发现缺失或不匹配的注释：

```ini
EXTRACT_ALL           = NO
EXTRACT_PRIVATE       = NO
WARNINGS              = YES
WARN_IF_UNDOCUMENTED  = YES
WARN_IF_DOC_ERROR     = YES
WARN_AS_ERROR         = NO
```

当前源码仍有未补齐的成员注释、参数名不匹配及继承构造函数解析警告。
这些警告会输出到终端，目前不会阻止生成文档。
`EXTRACT_ALL = YES` 会将未写文档的实体也视为已文档化，并关闭缺失文档警告，
因此需要检查注释完整性时应保留 `NO`。

清理完警告后，如需让自动化检查在出现警告时失败，可设置
`WARN_AS_ERROR = FAIL_ON_WARNINGS`。

Markdown 中提到注释标签时使用行内代码，例如 `@param`；
包含井号的协议文本也应使用行内代码，例如 `#HttpOnly_`，避免被解释为文档命令或链接。
代码注释的布局规则见 [仓库规范](../AGENTS.md)。

使用指南中的 Markdown 页面链接会转换为生成站点内的链接。
指向未纳入文档输入的配置、脚本或工作流文件的相对链接仍需在源码仓库中查看。

## 可选的关系图

需要 Graphviz 绘制类关系图时，先安装 Graphviz，确认 `dot -V` 可执行，再设置：

```ini
HAVE_DOT = YES
```

默认 HTML 文档和源码浏览不依赖 Graphviz；PDF 所需的 LaTeX 工具链也不属于当前配置。
