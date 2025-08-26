#!/usr/bin/env python3
"""
Build script for py-ministring PyPI package
"""
import subprocess
import sys
import os

def run_command(cmd, description):
    """Run a command and handle errors"""
    print(f"\n🔨 {description}...")
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"❌ {description} failed:")
        print(result.stderr)
        sys.exit(1)
    else:
        print(f"✅ {description} completed successfully")
    return result.stdout

def main():
    print("🚀 Building py-ministring for PyPI upload")

    # Clean previous builds
    run_command("python -m build --clean", "Cleaning previous builds")

    # Build source distribution
    run_command("python -m build --sdist", "Building source distribution")

    # Build wheel (if possible on this platform)
    try:
        run_command("python -m build --wheel", "Building wheel")
    except:
        print("⚠️  Wheel build failed - this is expected for C extensions on some platforms")

    # Check the built packages
    run_command("python -m twine check dist/*", "Checking built packages")

    print("\n🎉 Build completed! Files in dist/ are ready for PyPI upload")
    print("📁 Next steps:")
    print("   1. Test upload: python -m twine upload --repository testpypi dist/*")
    print("   2. Real upload: python -m twine upload dist/*")

if __name__ == "__main__":
    main()
