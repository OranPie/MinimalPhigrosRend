# Chart Loader

A unified chart loading system for Phigros charts that supports multiple formats and packaging methods.

## Features

- **FolderChart**: Load charts from directories with multiple difficulty files
- **ZipChart**: Load charts from zip archives (Phira format)
- **JSONChart**: Load standalone JSON files with automatic asset discovery
- **Asset Autofill**: Automatically finds music and illustration files

## Supported Formats

### Folder Structure
```
charts/
  ATHAZA.LeaF/
    IN.json          # Chart file (difficulty: IN)
    AT.json          # Chart file (difficulty: AT)
    ATHAZA.LeaF.ogg  # Music (auto-detected)
    ATHAZA.LeaF.png  # Illustration (auto-detected)
```

### Zip Structure (Phira format)
```
Horizon.Eason_AC.21311.zip
  3180987.json       # Chart file
  3180987.mp3        # Music
  3180987.jpg        # Illustration
  info.txt           # Metadata (optional)
  info.yml           # Metadata (optional)
```

### Standalone JSON
```
charts/
  my_chart.json      # Chart file
  my_chart.ogg       # Music (auto-detected)
  my_chart.png       # Illustration (auto-detected)
```

## Usage

### Scanning a Directory

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

### Loading from Zip

```cpp
// Check if path is a zip reference
if (phigros::chart::is_zip_path(entry.chart_path)) {
    auto [zip_path, file_in_zip] = phigros::chart::split_zip_path(entry.chart_path);

    // Extract chart data
    auto data = phigros::chart::extract_zip_file(zip_path, file_in_zip);

    // Parse JSON
    std::string json_str(data.begin(), data.end());
    auto j = nlohmann::json::parse(json_str);

    // Load chart
    auto chart = phigros::chart::parse_official(j, 1280, 720);
}
```

### Loading from File

```cpp
// Regular file loading
std::ifstream f(entry.chart_path);
auto j = nlohmann::json::parse(f);
auto chart = phigros::chart::parse_official(j, 1280, 720);
```

## API Reference

### Types

#### `ChartEntry`
```cpp
struct ChartEntry {
    std::string name;            // Song name
    std::string difficulty;      // "EZ", "HD", "IN", "AT", "SP", "EX"
    std::string chart_path;      // Path to chart file (or "zip:file")
    ChartAssets assets;          // Music, illustration, etc.
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

### Functions

#### `scan_charts_directory(dir_path)`
Scans a directory for all charts (folders, zips, standalone files).

#### `load_folder_chart(folder_path)`
Loads all difficulty charts from a folder.

#### `load_zip_chart(zip_path)`
Loads chart from a zip archive.

#### `load_json_chart(json_path)`
Loads a standalone JSON chart with asset autofill.

#### `extract_zip_file(zip_path, file_in_zip)`
Extracts a file from a zip archive to memory.

#### `is_zip_path(path)`
Checks if a path is a zip reference (format: "path.zip:file.json").

#### `split_zip_path(path)`
Splits a zip path into zip file and internal file.

## Tools

### chart_scanner
Command-line tool to scan and display charts in a directory.

```bash
./chart_scanner charts/
```

Output:
```
Found 15 chart(s):

Name                    Diff    Type      Music   Image   Path
----------------------------------------------------------------
ATHAZA.LeaF            IN      folder    ✓     ✓     charts/ATHAZA.LeaF/IN.json
ATHAZA.LeaF            AT      folder    ✓     ✓     charts/ATHAZA.LeaF/AT.json
Horizon.Eason_AC       -       zip       ✓     ✓     charts/Horizon.zip:3180987.json
...
```

## Asset Detection

The loader automatically detects assets based on:

1. **Same base name**: `ATHAZA.LeaF.json` → `ATHAZA.LeaF.ogg`, `ATHAZA.LeaF.png`
2. **Same directory**: Any `.ogg/.mp3/.wav` and `.png/.jpg/.jpeg` in the same folder
3. **Zip contents**: First matching file of each type in the zip

### Supported Extensions

- **Music**: `.ogg`, `.mp3`, `.wav`
- **Illustration**: `.png`, `.jpg`, `.jpeg`, `.webp`
- **Charts**: `.json`, `.pec`, `.phbc`

## Integration Example

See `examples/chart_loader_example.cpp` for a complete example of:
- Scanning a directory
- Loading charts from different sources
- Handling assets
- Parsing chart data

## Notes

- Zip paths use the format `"path.zip:file.json"` to indicate files inside archives
- Assets in zips must be extracted before use
- The loader sorts charts by name and difficulty (EZ < HD < IN < AT < SP < EX)
- All functions handle errors gracefully and return empty results on failure
