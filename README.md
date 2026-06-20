# fps-reverse-planning-server-research
Re-planning and implementing Overwatch's server system

## 계획
- 오버워치의 서버에 관한 역기획을 통해 하나의 fps 게임 서버를 제작
- 인증, 매칭 : ASP.NET API 서버
- 코어 로직 : Native C++ 서버
- Nginx 로드밸런싱

## Logic Server
- language : C++
  - version : c++20↑
- server libraries :
  - Asio
  - Protobuf
  - spdlog
  - stduuid
- OS : cross-platform
- IDE : Visual Studio, Visual Studio Code
- Build Systems : CMake with Ninja

## Auth, Matching Server
- language : C#
- OS : docker container based cross-platform
- framework : ASP.NET
- tech stacks :
  - Scalar UI
  - Redis Cache
