# GitHub Actions CI/CD Workflows

This directory contains automated workflows for continuous integration and deployment of the µC/OS-II ARMv8 bare-metal firmware project.

## 工作流程說明 / Workflows Overview

### 1. CI/CD Pipeline (`ci.yml`)

**Triggers / 觸發條件:**
- Push to `main` branch or `claude/**` branches
- Pull requests to `main` branch
- Manual workflow dispatch

**Jobs / 工作內容:**

#### Build and Test
- Installs AArch64 cross-compilation toolchain (gcc-aarch64-linux-gnu)
- Builds the firmware (`make all`)
- Verifies build artifacts
- Runs context switch and timer tests
- Runs network initialization tests
- Uploads firmware artifacts (retained for 30 days)

#### Code Quality
- Checks for large files
- Lists source files
- Validates Makefile syntax

**Artifacts / 產出物:**
- `firmware-artifacts`: Contains `kernel.elf` and `os.list`
- `test-binaries`: Contains test executables

---

### 2. Release Workflow (`release.yml`)

**Triggers / 觸發條件:**
- Push of version tags (e.g., `v1.0.0`, `v2.1.3`)
- Manual workflow dispatch with version input

**Process / 流程:**
1. Builds firmware and test binaries
2. Collects build information (version, commit, date, size)
3. Packages release artifacts into a tarball
4. Creates a GitHub Release with:
   - Firmware binary (`kernel.elf`)
   - Disassembly listing (`os.list`)
   - Test binaries
   - Build information
   - Quick start instructions

**Creating a Release / 建立發布版本:**
```bash
# Create and push a version tag
git tag -a v1.0.0 -m "Release version 1.0.0"
git push origin v1.0.0

# Or use the manual workflow dispatch from GitHub Actions UI
```

---

### 3. Pull Request Checks (`pr-check.yml`)

**Triggers / 觸發條件:**
- Pull request opened, synchronized, or reopened

**Validation Steps / 驗證步驟:**
1. **Change Detection**: Identifies modified files (Makefile, source files)
2. **Full Clean Build**: Performs `make remove && make all`
3. **Build Consistency**: Rebuilds to verify reproducibility
4. **Test Suite**: Runs all available tests
   - Context switch & timer test
   - Network initialization test
5. **Issue Detection**: Checks for:
   - Large object files (>100KB)
   - Stack usage warnings
   - Missing linker script
6. **Size Report**: Generates firmware size analysis
7. **PR Comment**: Posts build results and size report as a comment

**Artifacts / 產出物:**
- PR-specific build artifacts (retained for 7 days)
- Size report for comparison

---

## Local Testing / 本地測試

Before pushing, you can verify the build locally:

```bash
# Full build
make clean
make all

# Run tests that don't require TAP interfaces
make test-context
make test-net-init

# Check firmware size
aarch64-linux-gnu-size bin/kernel.elf
```

---

## Toolchain Requirements / 工具鏈需求

The CI workflows use:
- **GCC**: `gcc-aarch64-linux-gnu`
- **Binutils**: `binutils-aarch64-linux-gnu`
- **QEMU**: `qemu-system-aarch64`
- **Make**: GNU Make

All dependencies are installed automatically in the workflow.

---

## Workflow Status Badges / 工作流程狀態徽章

The following badges are displayed in the main README:

```markdown
[![CI/CD Pipeline](https://github.com/jhtwu/ucosii-aarch64/actions/workflows/ci.yml/badge.svg)](https://github.com/jhtwu/ucosii-aarch64/actions/workflows/ci.yml)
[![Pull Request Checks](https://github.com/jhtwu/ucosii-aarch64/actions/workflows/pr-check.yml/badge.svg)](https://github.com/jhtwu/ucosii-aarch64/actions/workflows/pr-check.yml)
[![Release](https://github.com/jhtwu/ucosii-aarch64/actions/workflows/release.yml/badge.svg)](https://github.com/jhtwu/ucosii-aarch64/actions/workflows/release.yml)
```

---

## Limitations / 限制

### Network Tests in CI
Tests requiring TAP interfaces (`test-ping`, `test-ping-wan`, `test-dual`) are skipped or run with `|| true` in CI because:
- GitHub Actions runners don't have TAP interface access
- Creating bridge networks requires elevated privileges
- These tests are better suited for local development environments

### Performance Tests
Tests with specific network performance requirements (vhost-net, multi-queue) cannot be fully validated in CI and should be run locally with appropriate setup.

---

## Troubleshooting / 疑難排解

### Workflow Fails at Build Step
- Check that all source files are committed
- Verify Makefile syntax locally: `make -n all`
- Ensure no missing dependencies in source files

### Test Failures
- Review test output in workflow logs
- Run tests locally to reproduce: `make test-context`
- Check QEMU version compatibility

### Artifact Upload Issues
- Verify paths exist after build
- Check artifact size limits (GitHub has size restrictions)
- Ensure proper permissions on generated files

---

## Future Enhancements / 未來改進

Potential improvements to the CI/CD pipeline:
- [ ] Cross-platform builds (different toolchain versions)
- [ ] Code coverage reporting
- [ ] Static analysis integration (cppcheck, clang-tidy)
- [ ] Performance benchmarking
- [ ] Docker-based reproducible builds
- [ ] Automated changelog generation
- [ ] Nightly builds with extended test suite

---

## Contributing / 貢獻

When contributing:
1. All PRs will automatically trigger the PR check workflow
2. Ensure your changes pass all checks before requesting review
3. Check the PR comment for build size impact
4. Address any test failures shown in workflow logs

---

For more information about the project, see the [main README](../../README.md).
