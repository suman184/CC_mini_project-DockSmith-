# Docksmith - Quick Start Guide

Get Docksmith up and running in 5 minutes.

---

## 🚀 Setup (One-Time)

### Step 1: Create Required Directories

```bash
mkdir -p ~/.docksmith/images ~/.docksmith/layers
```

### Step 2: Create Base Image

```bash
cat > ~/.docksmith/images/base.json << 'EOF'
{
  "name": "base",
  "version": "1.0"
}
EOF
```

### Step 3: Setup Docksmith Project

```bash
cd docksmith
gcc -o docksmith main.c

# Clean up any old build
rm -rf temp_fs
mkdir -p temp_fs/bin
cp /bin/sh temp_fs/bin/
```

✅ **You're ready!** Only do this once.

---

## 📝 Create Your First Build File

Create `docksmith/Docksmithfile`:

```dockerfile
FROM base
COPY . /app
ENV APP_NAME=MyApp
WORKDIR /app
CMD ["echo", "Application ready"]
```

---

## 🏃 Run the Build

### On Linux

```bash
cd docksmith
sudo -E ./docksmith build
```

**Note**: Use `sudo -E` to preserve environment variables (needed to access `~/.docksmith/`)

**Expected Output:**
```
Build command triggered
Command: FROM | Args: base
-> Handling FROM
✅ Loaded base image: base
📦 Temp filesystem initialized
Command: COPY | Args: . /app
-> Handling COPY
📦 Layer created: sha256:a1b2c3d4e5f6...
📁 Stored at: ~/.docksmith/layers/a1b2c3d4e5f6....tar
Command: ENV | Args: APP_NAME=MyApp
-> Handling ENV
✅ ENV added: APP_NAME=MyApp
Command: WORKDIR | Args: /app
-> Handling WORKDIR
✅ WorkingDir set to: /app
Command: CMD | Args: ["echo", "Application ready"]
-> Handling CMD
✅ CMD set to: echo Application ready

📦 Layers Built:
Layer 1:
  Digest: sha256:a1b2c3d4e5f6...
  Size: 12345 bytes
  CreatedBy: COPY
```

✅ **Success!** Your image has been built with isolated layers.

---

### On macOS

```bash
cd docksmith
./docksmith build
```

**Expected Output (without RUN):**
```
Build command triggered
-> Handling FROM
✅ Loaded base image: base
📦 Temp filesystem initialized
-> Handling COPY
📦 Layer created: sha256:...
-> Handling ENV
✅ ENV added: APP_NAME=MyApp
-> Handling WORKDIR
✅ WorkingDir set to: /app
-> Handling CMD
✅ CMD set to: echo Application ready
📦 Layers Built:
...
```

⚠️ **Note**: RUN instructions will fail on macOS due to System Integrity Protection (this is expected).

---

## 🧪 Example 1: Simple Build (Works on macOS & Linux)

**Docksmithfile:**
```dockerfile
FROM base
COPY . /app
ENV VERSION=1.0
WORKDIR /app
```

**Run:**
```bash
./docksmith build
```

**You'll see:**
- ✅ Base image loaded
- ✅ Files copied to temp_fs
- ✅ Environment variable stored
- ✅ Working directory set
- ✅ Layer created with SHA256

---

## 🧪 Example 2: With RUN (Linux Only)

**Docksmithfile:**
```dockerfile
FROM base
RUN mkdir -p /output
RUN echo "Hello Container" > /output/file.txt
```

**Run on Linux:**
```bash
sudo -E ./docksmith build
```

**You'll see:**
- ✅ FROM executed
- ✅ RUN 1: mkdir isolated in chroot
- ✅ RUN 2: file creation isolated in chroot
- ✅ Both layers created with unique SHA256 hashes
- ✅ Check output: `cat docksmith/temp_fs/output/file.txt`

**On macOS:**
```
❌ chroot() failed: Operation not permitted
```
(Expected - use Linux for testing RUN)

---

## 🧪 Example 3: Verify Isolation (Linux)

**Docksmithfile:**
```dockerfile
FROM base
RUN whoami > /identity.txt
RUN pwd >> /identity.txt
```

**Run:**
```bash
sudo -E ./docksmith build
cat docksmith/temp_fs/identity.txt
```

**Output:**
```
root
/
```

✅ **Proves isolation**: Command is running inside isolated chroot with its own root

---

## 📂 What Gets Created

After running `./docksmith build`:

```
~/.docksmith/
├── images/
│   └── base.json              ← Your base image
└── layers/
    ├── a1b2c3d4e5f6....tar    ← Layer 1 (COPY)
    └── f6e5d4c3b2a1....tar    ← Layer 2 (RUN)

docksmith/
├── temp_fs/                   ← Container root during build
│   ├── bin/
│   │   └── sh
│   ├── app/                   ← Files from COPY
│   ├── output/                ← Created by RUN
│   └── ...
└── docksmith                  ← Binary
```

---

## ❌ Common Issues & Fixes

### Issue: "Base image not found"
**Fix:**
```bash
mkdir -p ~/.docksmith/images
cat > ~/.docksmith/images/base.json << 'EOF'
{"name": "base", "version": "1.0"}
EOF
```

### Issue: "Permission denied" (macOS)
**Expected behavior** - chroot needs elevated privileges. Try:
```bash
sudo ./docksmith build
```

### Issue: "chroot() failed: Operation not permitted" (macOS)
**Expected on macOS** due to System Integrity Protection (SIP). Use Linux for RUN tests.

### Issue: "execl() failed: No such file or directory"
**Fix**: Ensure `/bin/sh` is in temp_fs:
```bash
mkdir -p docksmith/temp_fs/bin
cp /bin/sh docksmith/temp_fs/bin/
```

### Issue: "Docksmithfile not found"
**Fix**: Make sure you're in the docksmith directory:
```bash
cd docksmith
./docksmith build
```

---

## 🔍 Verify It's Working

### Check Layers Were Created
```bash
ls -la ~/.docksmith/layers/
# Should show .tar files with SHA256 names
```

### Check temp_fs Contents
```bash
ls -la docksmith/temp_fs/
# Should show files from COPY instruction
```

### Check Layer Size
```bash
du -h ~/.docksmith/layers/*.tar
# Shows size of each layer
```

### View Layer Metadata
The build output shows:
```
📦 Layer created: sha256:abc123...
📁 Stored at: ~/.docksmith/layers/abc123.tar
```

---

## 📊 What Each Instruction Does

| Instruction | Behavior | Creates Layer? |
|------------|----------|--------|
| FROM | Loads base image | ❌ No |
| COPY | Copies files to temp_fs | ✅ Yes |
| RUN | Executes command in chroot | ✅ Yes |
| ENV | Stores environment variable | ❌ No |
| WORKDIR | Sets working directory | ❌ No |
| CMD | Stores default command | ❌ No |

---

## 🎯 Next Steps

1. **Try Example 1** - Verify COPY and ENV work
2. **If on Linux** - Try Example 2 with RUN
3. **Check Layers** - Look in `~/.docksmith/layers/`
4. **Experiment** - Modify Docksmithfile and rebuild

---

## 📚 For More Info

- **Full Documentation**: See [README.md](README.md)
- **Implementation Details**: Section "Process Isolation with Chroot"
- **Architecture**: ASCII diagram in README.md

---

## ✅ Quick Checklist

- [ ] Created `~/.docksmith/images/` directory
- [ ] Created `base.json` 
- [ ] Compiled docksmith: `gcc -o docksmith main.c`
- [ ] Have `/bin/sh` in `temp_fs/bin/`
- [ ] Created `Docksmithfile`
- [ ] Ran `./docksmith build`
- [ ] Checked output for `✅` messages
- [ ] Verified layers in `~/.docksmith/layers/`

If all checked ✅ - **You're running Docksmith!** 🎉

---

**Last Updated**: March 29, 2026
