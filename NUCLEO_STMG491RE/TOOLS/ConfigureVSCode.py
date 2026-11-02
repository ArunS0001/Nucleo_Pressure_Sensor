import os
import json

# ==================== CONFIGURATION ====================
TARGET_DIR = r"D:\0cc_views\1409_SEP\TEST_1409"
COMPILER_PATH = r"C:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740\tools\bin\arm-none-eabi-gcc.exe"
# =======================================================

VSCODE_DIR = os.path.join(TARGET_DIR, ".vscode")

# 1. Corrected Configuration for IntelliSense (c_cpp_properties.json)
c_cpp_props = {
    "configurations": [
        {
            "name": "STM32G491",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/Core/Inc",
                "${workspaceFolder}/Drivers/STM32G4xx_HAL_Driver/Inc",
                "${workspaceFolder}/Drivers/CMSIS/Device/ST/STM32G4xx/Include",
                "${workspaceFolder}/Drivers/CMSIS/Include",
                "${workspaceFolder}/Drivers/BSP/STM32G4xx_Nucleo" # Path to resolve missing BSP buttons/LEDs
            ],
            "defines": [
                "DEBUG",
                "USE_HAL_DRIVER",
                "STM32G491xx"
            ],
            "compilerPath": COMPILER_PATH,
            "cStandard": "c11",
            "cppStandard": "c++17",
            "intelliSenseMode": "gcc-arm",
            "compilerArgs": [
                "-mcpu=cortex-m4",
                "-mthumb"
            ]
        }
    ],
    "version": 4
}

# 2. Corrected Core compiler arguments matching your actual project tree
BASE_ARGS = [
    "-g",
    "-mcpu=cortex-m4",
    "-mthumb",
    "--specs=nano.specs",
    
    # Correct paths prefixed with Core/
    "${workspaceFolder}/Core/Src/*.c",
    "${workspaceFolder}/Core/Startup/*.s",
    
    # Also compile the HAL Driver source files if they exist in your project
    "${workspaceFolder}/Drivers/STM32G4xx_HAL_Driver/Src/*.c",
    "${workspaceFolder}/Drivers/BSP/STM32G4xx_Nucleo/*.c", # Compiles the BSP code for buttons
    
    # Direct Include Paths
    "-I${workspaceFolder}/Core/Inc",
    "-I${workspaceFolder}/Drivers/STM32G4xx_HAL_Driver/Inc",
    "-I${workspaceFolder}/Drivers/CMSIS/Device/ST/STM32G4xx/Include",
    "-I${workspaceFolder}/Drivers/CMSIS/Include",
    "-I${workspaceFolder}/Drivers/BSP/STM32G4xx_Nucleo",
    
    # Required Core Definitions
    "-DDEBUG",
    "-DUSE_HAL_DRIVER",
    "-DSTM32G491xx",
    "-Wl,--gc-sections"
]

# 3. Dual Build Configuration Tasks (tasks.json)
tasks = {
    "version": "2.0.0",
    "tasks": [
        {
            "label": "Build STM32 (FLASH Target)",
            "type": "shell",
            "command": COMPILER_PATH,
            "args": BASE_ARGS + [
                "-T${workspaceFolder}/STM32G491RETX_FLASH.ld",
                "-Wl,-Map=${workspaceFolder}/build/output_flash.map",
                "-o", "${workspaceFolder}/build/output_flash.elf"
            ],
            "options": {"cwd": "${workspaceFolder}"},
            "group": {
                "kind": "build",
                "isDefault": True
            },
            "problemMatcher": "$gcc"
        },
        {
            "label": "Build STM32 (RAM Target)",
            "type": "shell",
            "command": COMPILER_PATH,
            "args": BASE_ARGS + [
                "-T${workspaceFolder}/STM32G491RETX_RAM.ld",
                "-Wl,-Map=${workspaceFolder}/build/output_ram.map",
                "-o", "${workspaceFolder}/build/output_ram.elf"
            ],
            "options": {"cwd": "${workspaceFolder}"},
            "group": "build",
            "problemMatcher": "$gcc"
        }
    ]
}

def setup_vscode_config():
    if not os.path.exists(TARGET_DIR):
        print(f"Error: Target directory '{TARGET_DIR}' does not exist.")
        return

    os.makedirs(VSCODE_DIR, exist_ok=True)
    os.makedirs(os.path.join(TARGET_DIR, "build"), exist_ok=True)

    with open(os.path.join(VSCODE_DIR, "c_cpp_properties.json"), "w", encoding="utf-8") as f:
        json.dump(c_cpp_props, f, indent=4)

    with open(os.path.join(VSCODE_DIR, "tasks.json"), "w", encoding="utf-8") as f:
        json.dump(tasks, f, indent=4)
        
    print(f"Successfully generated updated configurations matching the Core/ tree structure.")

if __name__ == "__main__":
    setup_vscode_config()
