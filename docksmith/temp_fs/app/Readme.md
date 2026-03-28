# Docksmith (Till Current Progress)

## 🚀 What is this project?

Docksmith is a simplified version of Docker that we are building from scratch.
The goal is to understand how Docker actually works internally like:

* How images are built
* How layers work
* How containers run

---

## 🧠 What we have built till now

### 1. CLI (Command Line Interface)

We created a basic CLI so that we can run commands like:

```
./docksmith build
```

👉 This is the entry point of our system.

---

### 2. Reading Docksmithfile

We created a file called `Docksmithfile` which contains instructions.

Example:

```
FROM base
WORKDIR /app
COPY . /app
RUN echo "Hello"
ENV NAME=Zoro
CMD ["echo", "Done"]
```

👉 Our program reads this file line by line using `fgets()`.

---

### 3. Parsing Instructions

Each line is split into:

* `command` (like FROM, COPY, RUN)
* `args` (rest of the line)

Example:

```
RUN echo Hello
↓
command = RUN
args = echo Hello
```

👉 We used `sscanf()` for this.

---

### 4. Dispatcher (Decision Making)

We added logic like:

```
if command == FROM → handle FROM
if command == COPY → handle COPY
```

👉 This is like the brain of our build system.

---

### 5. FROM (Base Image Loading)

We implemented FROM so that:

* It checks if base image exists in `~/.docksmith/images/`
* Loads that image
* Initializes our build

👉 This is where the build actually starts.

---

### 6. Image State (Struct)

We created a struct:

```
typedef struct {
    name
    workingDir
    env variables
    cmd
} Image;
```

👉 This stores everything about the image being built.

---

### 7. WORKDIR

We implemented WORKDIR:

```
WORKDIR /app
```

👉 This updates:

```
currentImage.workingDir = "/app"
```

---

### 8. ENV (Environment Variables)

We implemented ENV:

```
ENV NAME=Zoro
```

👉 This stores key-value pairs inside struct.

Important:

* We are only storing it now
* It will be used later during RUN and container execution

---

### 9. CMD (Default Command)

We implemented CMD:

```
CMD ["echo", "Done"]
```

👉 This stores the default command inside struct.

---

### 10. COPY (Basic Implementation)

We implemented a simple version of COPY:

```
COPY . /app
```

👉 What we did:

* Created a temporary folder `temp_fs`
* Copied files into it

```
cp -r ./* temp_fs/app/
```

👉 This simulates container filesystem.

---

## 🔥 Current Flow

```
Docksmithfile
   ↓
Read line
   ↓
Parse command + args
   ↓
Dispatcher decides
   ↓
Execute logic
```

---

## 📊 Current Progress

### Build Engine: ~55% Done

✔ CLI
✔ Parsing
✔ Dispatcher
✔ FROM
✔ WORKDIR
✔ ENV
✔ CMD
✔ COPY (basic)

---

## ❌ What is left

* Proper COPY (layer creation)
* RUN (execute inside isolated environment)
* Layer system (tar + hashing)
* Build cache
* Manifest creation
* Container runtime

---

## 🧠 Key Understanding

* Docksmithfile is NOT encrypted → it's plain text
* We are building a system that **reads instructions and executes them step-by-step**
* Everything revolves around **state (Image struct)**

---

## 🎯 Next Steps

* Implement RUN (most important + tricky part)
* Start creating layers
* Move towards real container behavior

---

## 💯 Final Note

Right now we have built:
👉 The **foundation + control flow**

Next:
👉 We build the **actual engine**

---
