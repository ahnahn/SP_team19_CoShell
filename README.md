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



---

## ✨ Features

* ✅ **To-Do List**  
  You can record a list of tasks that team members need to do.
  The list window is fixed on the left box of CoShell, and you can add, delete, and modify the list. You can also check the box next to the list to indicate whether it has already been completed or is in progress.

* ✅ **Chat**  
  Team members can chat in real time. You can enter the server port number and set a nickname to distinguish between members.
  You can update the list in To-Do-List in real time using commands such as `/add`, `/del`, `/edit` while chatting with members in real time.

* ✅ **QR Generator**  
  In conference or study rooms, you can share data via QR codes without running a chat or file server, avoiding any cumbersome upload/download steps.
  To generate a QR code, simply specify the absolute Linux path of the file you want to encode (e.g., a C source file or a plain‑text file).
  Because the dimensions of the QR code grow with the amount of data, maximize your terminal window before using this feature.
  A Version 40‑L QR code can hold up to about 2.9 KB of data, but depending on your device the QR image may become too large and get clipped in the Linux terminal window—so it’s limited to 700 bytes.

* ✅ **World Clock**  
  When collaborating, you can fix the current Local time zone and set up to 2 world time zones, considering members in different time zones.
  When you press function 4, the world time zones that can be changed will appear, and you can enter the index of the time zone to be changed (index 1, 2 in order of time zones under Local) and enter the index of the time to be changed.    

* ✅ **CLI Mode Support**  
  You can use all ToDo features directly via CLI without launching the UI.  
  For example:
  ```bash
  ./coshell add "Fix README formatting"
  ./coshell done 3
  ./coshell list
  ./coshell edit 2 "Update contribution section"
  ./coshell del 1
  ```



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



---

## 🧭 Usage Guide

### 🔘 Main Menu

After launching `./coshell`, you'll see the following options:

1. **Start Chat Server with Serveo**

   * Opens a tunnel with [serveo.net](https://serveo.net) to make your chat server accessible.
   * ⚠️ If Serveo is unavailable (e.g., blocked on your network), the server will fall back to `localhost:12345`.
   * ✅ Press `1` to run this option.
   * 🔁 Keep this server running before entering chat mode from the UI.

2. **Launch CoShell UI**

   * Press `2` to enter the main ncurses-based interface.
   * Once inside the UI, you'll see options like:



     * 📝 **ToDo Mode**: Manage your local task list. (Press `1`)

Press 1 to access the following commands:
add <item>
done <num>
undo <num>
del <num>
edit <num> <new item>

Below is an example managing an item called "foo":

1. In coshell’s command line at the very bottom, type:
   add foo
   You’ll see "1. foo [ ]" appear in the To‑Do List on the right. Each time you add an item, its index number on the left increments by one.

2. Once you’ve completed the task, type:
   done 1
   The first item will be marked as done, showing:
   1. foo [x]

3. If you change your mind, type:
   undo 1
   to remove the checkmark.

4. To delete an item, type:
   del 1
   (with a space after "del"), and the item at index 1 will be removed.

5. To rename an item, type:
   edit 1 flag
   which changes "foo" at index 1 into "flag", for example:
   1. flag [ ]
6. At any time, type `q` to return to the lobby.

  
     * 💬 **Chat Mode**: Chat with other users in real time. (Press `2`)

Press 2 to start the chat feature:

1. You will be prompted for the chat host. Since coshell uses Serveo, enter:
   serveo.net

2. Next, enter the port number created when you started the coshell server, immediately followed (no spaces) by the nickname you want to use in chat.

3. You can then chat with your team members—and at the same time use the To-Do List commands. Just prepend a slash ("/") to any To-Do command exactly as before, for example:
   /add foo
   /del 1
   /done 1

4. At any point during the chat, type:
   /quit
   to return to the lobby.

       
     * 📷 **QR Mode**: Generate QR codes from files or text input. (Press `3`)
  
Press 3 to generate and read QR codes from text-based files (e.g., C source or .txt):

1. Enter the absolute Linux path of the file you wish to convert, for example:
   /home/user/temp/foo.c

2. After entering the path, press any key to generate and display the QR code.  
   Press `q` to return to the lobby.

Note: Since QR code dimensions grow with file size, maximize your terminal window first to avoid clipping. To accommodate different device display sizes, QR generation is limited to files no larger than 700 bytes. If your file exceeds 700 bytes, the tool will show its size and display an error indicating the limit has been exceeded, so you can confirm compatibility.



     * 🌐 **Clock Mode**: Set and display global time zones alongside your local time. (Press `4`)

Press 4 to change the timezones displayed at the top of CoShell:

The Timebox’s Local time is fixed, but the two initially set zones (USA ET and UK GMT) can be modified.

1. After pressing 4, a list of available timezones will appear.
2. Under the Timebox’s Local time, the two slots for additional timezones are indexed as 1 and 2.
3. Use these indexes to place your desired timezone into the corresponding slot.  
   For example, option 9 represents Japan JST. To replace USA ET (slot 1) with JST, enter:
   1 9

At any time, press `q` to return to the lobby.





   **💬 Chat Mode details:**

   * After entering Chat Mode, you'll be prompted to enter:

     1. **Host name** – Choose from:

        * `serveo.net`: If you started the chat server with option 1.
        * `localhost`: If Serveo is blocked or unavailable.
     2. **Port number** – Depends on the host:

        * If using `serveo.net`: Input the **Serveo-assigned port** (shown when option 1 is started).
        * If using `localhost`: Input `12345`.

   * Then enter your nickname to join the chat room.

   * ⚠️ Ensure the chat server is already running before connecting.

4. **CLI Mode**

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



---

## 👥 Contributors

<table>
  <tr>
    <td align="center"><a href="https://github.com/ahnahn"><img src="https://github.com/ahnahn.png" width="100px;" alt="ahn"/><br /><sub><b>ahnahn</b></sub></a></td>
    <td align="center"><a href="https://github.com/GeonwooLee21"><img src="https://github.com/GeonwooLee21.png" width="100px;" alt="geonwoo"/><br /><sub><b>GeonwooLee21</b></sub></a></td>
    <td align="center"><a href="https://github.com/lyjae"><img src="https://github.com/lyjae.png" width="100px;" alt="lyjae"/><br /><sub><b>lyjae</b></sub></a></td>
  </tr>
</table>



---

## 🙏 Acknowledgments

* [Ncurses Library](https://invisible-island.net/ncurses/)
* [qrencode](https://fukuchi.org/works/qrencode/)

<p align="right">(<a href="#readme-top">back to top</a>)</p>
