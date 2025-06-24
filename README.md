<a id="readme-top"></a>

<div align="center">
  <h1 align="center">CoShell</h1>
  <p align="center">A Terminal-Based Collaboration Toolbox</p>
  <a href="https://www.youtube.com/watch?v=-Ow-Q8T48PY"><strong>🎥 Watch Demo</strong></a>
  <br />
</div>

---

## 📋 Table of Contents

* [About The Project](#-about-the-project)
* [Features](#-features)
* [Build & Run](#-build--run)
* [Usage Guide](#-usage-guide)
* [Contributors](#-contributors)
* [Acknowledgments](#-acknowledgments)

---

## 🧠 About The Project

> **CoShell** is a terminal-based collaboration toolbox designed for CLI-first teamwork. It eliminates the need for external GUI collaboration tools by integrating core features into a single terminal UI.

<p align="right">(<a href="#readme-top">back to top</a>)</p>

---

## ✨ Features

* ✅ **To-Do List**: Create, complete, undo, delete, and edit tasks in a local user list.
* ✅ **Chat**: Real-time terminal chat with nickname and port customization.
* ✅ **QR Generator**: Generate QR codes for quick sharing of small data.
* ✅ **World Clock**: Track local and global time zones within the UI.
* ✅ **CLI Mode Support**: All features can also be used in CLI-based interaction without entering the UI mode.


<p align="right">(<a href="#readme-top">back to top</a>)</p>

---

## 🛠 Build & Run

### 🔧 Prerequisites

* OS: Linux/Unix

### 🏗 Build

```bash
make
```

### 🚀 Run

```bash
./coshell
```

<p align="right">(<a href="#readme-top">back to top</a>)</p>

---

## 🧭 Usage Guide

### 🔘 Main Menu

Upon running `./coshell`, the main menu offers the following options:

1. **Start Chat Server with Serveo**

   * Automatically opens a Serveo tunnel to expose your localhost.
   * ⚠️ If Serveo is blocked or unavailable, fallback to `localhost:12345`.

2. **Launch CoShell UI**

   * Terminal-based ncurses UI with the following modes:

     * **ToDo Mode**: Manage your personal or team ToDo list.
     * **Chat Mode**: Connect with team via terminal chat interface.
     * **QR Generator**: Input absolute path → outputs fullscreen QR.
     * **World Clock Mode**: Adjust and view time zones for remote members.

3. **CLI Mode**

   * Use `todo_client` or other tools to interact with the ToDo features outside the UI.
   * Example:

     ```bash
     ./todo_client add "Fix README formatting"
     ./todo_client list
     ```

<p align="right">(<a href="#readme-top">back to top</a>)</p>

---

## 👥 Contributors

<table>
  <tr>
    <td align="center"><a href="https://github.com/ahnahn"><img src="https://github.com/ahnahn.png" width="100px;" alt="ahn"/><br /><sub><b>ahnahn</b></sub></a></td>
    <td align="center"><a href="https://github.com/GeonwooLee21"><img src="https://github.com/GeonwooLee21.png" width="100px;" alt="geonwoo"/><br /><sub><b>GeonwooLee21</b></sub></a></td>
    <td align="center"><a href="https://github.com/lyjae"><img src="https://github.com/lyjae.png" width="100px;" alt="lyjae"/><br /><sub><b>lyjae</b></sub></a></td>
  </tr>
</table>

<p align="right">(<a href="#readme-top">back to top</a>)</p>

---

## 🙏 Acknowledgments

* [Best-README-Template](https://github.com/othneildrew/Best-README-Template)
* [Img Shields](https://shields.io)
* [Ncurses Library](https://invisible-island.net/ncurses/)
* [qrencode](https://fukuchi.org/works/qrencode/)

<p align="right">(<a href="#readme-top">back to top</a>)</p>
