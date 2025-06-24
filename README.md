물론입니다! 아래는 말씀하신 내용을 반영하여 `README.md` 전체 원형을 유지하면서 **채팅 진입 시 호스트/포트 입력 방식 및 서버가 먼저 열려 있어야 한다는 설명을 추가한 버전**입니다.

````markdown
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

> **CoShell** (Cooperating in Shell) is a terminal-based collaboration toolbox.  
> It enables seamless teamwork directly from the terminal, removing the need for external GUI tools.  
> Whether you're managing tasks, chatting in real-time, or sharing data over QR, everything happens right in your terminal.

<p align="right">(<a href="#readme-top">back to top</a>)</p>

---

## ✨ Features

* ✅ **To-Do List**  
  Add, complete, undo, delete, and edit tasks in a personal task list.  
  The list is displayed on the left pane of the UI.

* ✅ **Chat**  
  Real-time terminal chat between team members.  
  Users can enter a nickname and custom port.  
  You can also use chat commands like `/add`, `/del`, `/edit` to manage tasks live during conversation.

* ✅ **QR Generator**  
  Input an absolute path → outputs fullscreen QR code for easy file/data sharing.  
  Data over 700 bytes is automatically rejected to avoid terminal overflow.

* ✅ **World Clock**  
  Track your local time and up to two other world time zones.  
  You can set or change the world clocks using simple input like `1 9` (e.g., change ET to JST).

* ✅ **CLI Mode Support**  
  You can use all ToDo features directly via CLI without launching the UI.  
  For example:
  ```bash
  ./coshell add "Fix README formatting"
  ./coshell done 3
  ./coshell list
  ./coshell edit 2 "Update contribution section"
  ./coshell del 1
````

<p align="right">(<a href="#readme-top">back to top</a>)</p>

---

## 🛠 Build & Run

### 🔧 Prerequisites

* OS: Linux / Unix / Windows WSL

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

After launching `./coshell`, you'll see the following options:

1. **Start Chat Server with Serveo**

   * Opens a tunnel with [serveo.net](https://serveo.net) to make your chat server accessible.
   * ⚠️ If Serveo is unavailable (e.g., blocked on your network), the server will fall back to `localhost:12345`.

2. **Launch CoShell UI**

   * Enter the main ncurses-based interface with:

     * 📝 **ToDo Mode**: Manage your local task list.
     * 💬 **Chat Mode**: Chat with other users in real time.

       * ⚠️ Before entering Chat Mode, ensure the **chat server is already running** (via menu option 1 or a manual run).
       * You'll be prompted to enter a **host name** and **port**:

         * If using **Serveo** (e.g., after pressing `1` at main menu):

           * Enter `serveo.net` as the host name.
           * Then enter the **Serveo-assigned port number** that was shown when the server started.
         * If Serveo is blocked or unavailable:

           * Enter `localhost` as the host name.
           * Then enter `12345` as the port number (the fallback default).
     * 📷 **QR Mode**: Generate QR codes from files or text input.
     * 🌐 **Clock Mode**: Set and display global time zones alongside your local time.

3. **CLI Mode (Headless)**

   * You can use CoShell’s ToDo functionality without UI like this:

     ```bash
     ./coshell add "Implement global clock"
     ./coshell list
     ./coshell done 2
     ./coshell undo 2
     ./coshell edit 2 "Fix bug in clock rendering"
     ./coshell del 2
     ```

   * Each command prints a clear result or error message to the terminal.

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

* [Ncurses Library](https://invisible-island.net/ncurses/)
* [qrencode](https://fukuchi.org/works/qrencode/)

<p align="right">(<a href="#readme-top">back to top</a>)</p>
```
