plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
    alias(libs.plugins.kotlin.compose)
    id("kotlin-parcelize")
}

android {
    namespace   = "org.phigros.renderer"
    compileSdk  = 35

    defaultConfig {
        applicationId   = "org.phigros.renderer"
        minSdk          = 26
        targetSdk       = 35
        versionCode     = 1
        versionName     = "1.0"

        externalNativeBuild {
            cmake {
                cppFlags("-std=c++17")
                arguments(
                    "-DANDROID=1",
                    "-DBUILD_RENDER_APP=OFF",
                    "-DBUILD_PYTHON_BINDINGS=OFF",
                    "-DUSE_BGFX=OFF",
                    "-DUSE_SDL3=OFF",
                    "-DUSE_LIBAV=OFF",
                    "-DUSE_ENCRYPTION=OFF"
                )
                abiFilters += listOf("arm64-v8a", "x86_64")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path    = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"))
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions { jvmTarget = "17" }

    buildFeatures { compose = true }

    // Lock to landscape for gameplay
    defaultConfig.manifestPlaceholders["screenOrientation"] = "sensorLandscape"
}

dependencies {
    implementation(libs.core.ktx)
    implementation(libs.activity.compose)
    implementation(libs.lifecycle.runtime)
    implementation(libs.lifecycle.viewmodel)
    implementation(libs.coroutines.android)

    implementation(platform(libs.compose.bom))
    implementation(libs.compose.ui)
    implementation(libs.compose.ui.graphics)
    implementation(libs.compose.material3)
    implementation(libs.compose.preview)

    implementation(libs.media3.exoplayer)
}
