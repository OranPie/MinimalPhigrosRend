# 谱面加载器

> 🌐 [English](CHART_LOADER.md)

统一的 Phigros 谱面加载系统，支持多种格式和打包方式。

## 功能特性

- **FolderChart**：从包含多个难度文件的目录加载谱面
- **ZipChart**：从 zip 压缩包加载谱面（Phira 格式）
- **JSONChart**：加载独立 JSON 文件，自动发现关联资源
- **资源自动填充**：自动查找音乐和曲绘文件

## 支持的格式

### 目录结构
```
charts/
  ATHAZA.LeaF/
    IN.json          # 谱面文件（难度：IN）
    AT.json          # 谱面文件（难度：AT）
    ATHAZA.LeaF.ogg  # 音乐（自动检测）
    ATHAZA.LeaF.png  # 曲绘（自动检测）
```

### Zip 结构（Phira 格式）
```
Horizon.Eason_AC.21311.zip
  3180987.json       # 谱面文件
  3180987.mp3        # 音乐
  3180987.jpg        # 曲绘
  info.txt           # 元数据（可选）
  info.yml           # 元数据（可选）
```

### 独立 JSON
```
charts/
  my_chart.json      # 谱面文件
  my_chart.ogg       # 音乐（自动检测）
  my_chart.png       # 曲绘（自动检测）
```
## 使用方法

### 扫描目录

```cpp
#include "phigros/chart/chart_loader.hpp"

auto entries = phigros::chart::scan_charts_directory("charts/");

for (const auto& entry : entries) {
    std::cout << entry.name << " (" << entry.difficulty << ")\n";
    std::cout << "  Chart: " << entry.chart_path << "\n";
    std::cout << "  Music: " << entry.assets.music_path << "\n";
    std::cout << "  Image: " << entry.assets.illustration_path << "\n";
}
```

### 从 Zip 加载

```cpp
// 检查路径是否为 zip 引用
if (phigros::chart::is_zip_path(entry.chart_path)) {
    auto [zip_path, file_in_zip] = phigros::chart::split_zip_path(entry.chart_path);

    // 提取谱面数据
    auto data = phigros::chart::extract_zip_file(zip_path, file_in_zip);

    // 解析 JSON
    std::string json_str(data.begin(), data.end());
    auto j = nlohmann::json::parse(json_str);

    // 加载谱面
    auto chart = phigros::chart::parse_official(j, 1280, 720);
}
```

### 从文件加载

```cpp
// 常规文件加载
std::ifstream f(entry.chart_path);
auto j = nlohmann::json::parse(f);
auto chart = phigros::chart::parse_official(j, 1280, 720);
```
## API 参考

### 类型

#### `ChartEntry`
```cpp
struct ChartEntry {
    std::string name;            // 曲名
    std::string difficulty;      // "EZ", "HD", "IN", "AT", "SP", "EX"
    std::string chart_path;      // 谱面文件路径（或 "zip:file"）
    ChartAssets assets;          // 音乐、曲绘等
    std::string source_type;     // "folder", "zip", "json"
};
```

#### `ChartAssets`
```cpp
struct ChartAssets {
    std::string music_path;           // .ogg, .mp3, .wav
    std::string illustration_path;    // .png, .jpg, .jpeg
    std::vector<std::string> extra_files;
};
```

### 函数

#### `scan_charts_directory(dir_path)`
扫描目录中的所有谱面（目录、zip、独立文件）。

#### `load_folder_chart(folder_path)`
从目录加载所有难度的谱面。

#### `load_zip_chart(zip_path)`
从 zip 压缩包加载谱面。

#### `load_json_chart(json_path)`
加载独立 JSON 谱面并自动填充资源。

#### `extract_zip_file(zip_path, file_in_zip)`
将 zip 中的文件提取到内存。

#### `is_zip_path(path)`
检查路径是否为 zip 引用（格式："path.zip:file.json"）。

#### `split_zip_path(path)`
将 zip 路径拆分为 zip 文件路径和内部文件路径。
## 工具

### chart_scanner
用于扫描和显示目录中谱面的命令行工具。

```bash
./chart_scanner charts/
```

输出：
```
Found 15 chart(s):

Name                    Diff    Type      Music   Image   Path
----------------------------------------------------------------
ATHAZA.LeaF            IN      folder    ✓     ✓     charts/ATHAZA.LeaF/IN.json
ATHAZA.LeaF            AT      folder    ✓     ✓     charts/ATHAZA.LeaF/AT.json
Horizon.Eason_AC       -       zip       ✓     ✓     charts/Horizon.zip:3180987.json
...
```

## 资源检测

加载器根据以下规则自动检测资源：

1. **同名匹配**：`ATHAZA.LeaF.json` → `ATHAZA.LeaF.ogg`、`ATHAZA.LeaF.png`
2. **同目录匹配**：同一目录下的任意 `.ogg/.mp3/.wav` 和 `.png/.jpg/.jpeg` 文件
3. **Zip 内容匹配**：zip 中每种类型的第一个匹配文件

### 支持的扩展名

- **音乐**：`.ogg`、`.mp3`、`.wav`
- **曲绘**：`.png`、`.jpg`、`.jpeg`、`.webp`
- **谱面**：`.json`、`.pec`、`.phbc`

## 集成示例

参见 `examples/chart_loader_example.cpp`，包含以下完整示例：
- 扫描目录
- 从不同来源加载谱面
- 处理资源
- 解析谱面数据

## 注意事项

- Zip 路径使用 `"path.zip:file.json"` 格式表示压缩包内的文件
- Zip 中的资源需要先提取才能使用
- 加载器按曲名和难度排序（EZ < HD < IN < AT < SP < EX）
- 所有函数均优雅处理错误，失败时返回空结果
