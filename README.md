# Mini UnionFS (FUSE-based File System)

## 📌 Overview

This project implements a **Mini Union File System** using FUSE (Filesystem in Userspace).
It combines two directories:

* **Lower directory (read-only base layer)**
* **Upper directory (writable layer)**

The mounted filesystem presents a unified view of both layers.

---

## ⚙️ Features

* Layered file system (Upper + Lower)
* Copy-on-write mechanism
* Whiteout mechanism for file deletion
* File read/write operations via FUSE

---

## 🛠️ Tech Stack

* C programming
* FUSE (Filesystem in Userspace)
* Linux/Unix environment

---

## 📁 Project Structure

```
mini_unionfs.c     # Main implementation
Makefile           # Build instructions
test_unionfs.sh    # Test script
```

---

## 🚀 How to Run

### 1. Compile the code

```bash
make
```

### 2. Run the filesystem

```bash
./mini_unionfs <lower_dir> <upper_dir> <mount_point>
```

### 3. Example

```bash
mkdir lower upper mnt
./mini_unionfs lower upper mnt
```

---

## 🧪 Testing

Run the test script:

```bash
chmod +x test_unionfs.sh
./test_unionfs.sh
```

This script verifies:

* Layer visibility
* Copy-on-write behavior
* Whiteout mechanism

---

## 🧠 Key Concepts

### Copy-on-Write

When modifying a file:

* Original file remains in lower layer
* Modified version is stored in upper layer

### Whiteout Mechanism

When deleting a file:

* A hidden file (e.g., `.wh.filename`) is created in upper layer
* Prevents access to lower layer file

---

## 📌 Future Improvements

* Directory handling enhancements
* Better error handling
* Performance optimizations

---

## 👩‍💻 Author

Samiya Naaz, Sanjana Ramesh, Samsthuta Sridhar, Sangeetha BA
