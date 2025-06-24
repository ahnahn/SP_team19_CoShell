<a id="readme-top"></a>

<!-- PROJECT SHIELDS -->
[![Contributors][contributors-shield]][contributors-url]
[![License][license-shield]][license-url]

<!-- PROJECT LOGO -->
<br />
<div align="center">
  <h3 align="center">📋 CoShell ToDo 협업 시스템</h3>

  <p align="center">
    readme 테스트 ncurses 기반 협업 ToDo 관리 시스템<br/>
    로컬 모드 & 서버 연동 모드 지원!
    <br />
    <br />
    <a href="#demo">View Demo</a>
    ·
    <a href="https://github.com/your_username/coshell-todo/issues">Report Bug</a>
    ·
    <a href="https://github.com/your_username/coshell-todo/issues">Request Feature</a>
  </p>
</div>

---

## 📌 Table of Contents

- [About The Project](#about-the-project)
  - [Features](#features)
  - [Built With](#built-with)
- [Getting Started](#getting-started)
  - [Prerequisites](#prerequisites)
  - [Build & Run](#build--run)
- [Usage](#usage)
- [Demo](#demo)
- [Contact](#contact)
- [Acknowledgments](#acknowledgments)

---

## 💡 About The Project

**CoShell ToDo 시스템**은 팀 협업을 위한 터미널 기반 ToDo 관리 도구입니다. `ncurses`를 기반으로 하는 UI를 제공하며, 로컬 파일 기반의 개인 모드와 소켓 통신 기반의 팀 협업 모드를 전환할 수 있습니다.

이 프로젝트는 팀원 간 **작업 목록 공유**, **간단한 명령어 기반 조작**, 그리고 **UI 명확성**에 중점을 두고 개발되었습니다.

### ✅ Features

- [x] `ncurses` 기반 UI
- [x] 로컬 파일 기반 ToDo 저장/불러오기
- [x] TCP 소켓 기반 서버 연동 (`team` 모드)
- [x] 커맨드: `add`, `done`, `undo`, `del`, `edit`, `team`, `user`
- [x] 완료 항목 체크 표시 `[x]`/`[ ]`
- [x] `pthread` 기반 Mutex로 데이터 보호

### 🛠 Built With

- C (POSIX)
- ncurses
- pthread
- TCP Socket Programming

<p align="right">(<a href="#readme-top">back to top</a>)</p>

---

## 🚀 Getting Started

이 프로젝트는 Linux 환경에서 `make`를 사용해 쉽게 빌드하고 실행할 수 있습니다.




readme 테스트
### 📦 Prerequisites

- `gcc`
- `make`
- `libncurses-dev`
- `libpthread`

### 🔧 Build & Run

```bash
# 저장소 클론
git clone https://github.com/your_username/coshell-todo.git
cd coshell-todo

# 빌드
make

# 실행
./coshell
