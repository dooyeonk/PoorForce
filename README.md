# Poorforce

학생/소규모 팀용 "Poor man's perforce" — Unreal Editor 에서 동시 편집 충돌을 방지하는 Redis 기반 락 시스템 + (선택) Google Drive 동기화.

**대상**: GitHub Free LFS (락 미지원) 같은 환경에서 Perforce/Plastic 없이 협업 잠금만 필요할 때.

## 문서

- 📦 [설치 가이드 (docs/SETUP.md)](docs/SETUP.md) — 처음 설치 + 팀원 셋업
- 📖 [사용 매뉴얼 (docs/MANUAL.md)](docs/MANUAL.md) — 콘솔 커맨드, CI 워크플로우, 트러블슈팅

---

## 핵심 개념

### 두 가지 모드

관리 대상 폴더를 `PoorforceConfig.json` 의 `ManagedPaths` 로 등록. 등록 안 된 폴더는 plugin 이 일체 관여 안 함.

| 모드 | 락 (Redis) | 파일 저장 | 용도 |
|------|-----------|---------|------|
| `LockOnly` | ✅ | **Git LFS** (사용자가 git push/pull) | 일반 무료 에셋 (git 으로 관리) |
| `LockAndSync` | ✅ | **Google Drive + Rclone** (plugin 자동) | 유료 에셋 (git 제외) |

### 분리된 책임

- **Redis**: 락 상태의 single source of truth. 어떤 사용자가 어떤 에셋을 작업 중인지
- **Google Drive (LockAndSync)**: 유료 에셋의 실제 데이터. plugin 이 자동 동기화
- **Git LFS (LockOnly)**: 일반 에셋의 데이터. 사용자가 git push/pull. plugin 은 락만 추가
- **CI (GitHub Actions)**: PR 머지 시 자동으로 Redis 락 + LFS 락 해제

---

## 시스템 아키텍처

```mermaid
graph TD
    %% 1. 상단: 명령을 발생시키는 주체들 (로컬 & CI)
    subgraph Local["🖥 로컬 개발 환경"]
        subgraph DevA["개발자 A (Unreal Editor)"]
            PluginA["Poorforce Plugin"]
            WatcherA["Detached Watcher<br/>(PowerShell)"]
            RcloneA["rclone.exe"]
            GitA["git lfs"]
            
            %% 로컬 내부 상호작용 (점선)
            PluginA -.->|spawn| WatcherA
            PluginA -.->|spawn| RcloneA
            PluginA -.->|spawn| GitA
            WatcherA -.->|crash 감지| RcloneA
        end

        subgraph DevB["개발자 B (Unreal Editor)"]
            PluginB["Poorforce Plugin"]
        end
    end

    subgraph CI["🔧 CI/CD 파이프라인"]
        Actions["GitHub Actions<br/>PR Merged"]
        Scripts["release-locks.sh<br/>release-lfs-locks.sh"]
        
        Actions --> Scripts
    end

    %% 2. 하단: 명령을 수신하는 대상 (클라우드 서비스)
    subgraph Cloud["☁ Cloud Services"]
        Redis[("Upstash Redis<br/>락 상태 SSOT")]
        LFSServer[("GitHub LFS<br/>에셋 Lock 상태")]
        Drive[("Google Drive<br/>유료 에셋 저장소")]
        Discord[/"Discord Webhook<br/>강제 해제 알림"/]

        %% 투명 링크(~~~)를 이용한 2x2 격자 강제 배치 (가로 폭 축소)
        Redis ~~~ Drive
        LFSServer ~~~ Discord
    end

    %% 3. 외부 네트워크 통신 (실선: 위에서 아래로 흐름)
    %% 개발자 A 네트워크 통신
    PluginA -->|REST API: SET / GET / DEL| Redis
    GitA -->|lock / unlock| LFSServer
    RcloneA -->|copy / check| Drive
    PluginA -->|webhook POST| Discord
    WatcherA -.->|복구| Redis

    %% 개발자 B 네트워크 통신
    PluginB -->|REST API: GET / SET| Redis

    %% CI/CD 네트워크 통신
    Scripts -->|락 강제 DEL| Redis
    Scripts -->|unlock --force| LFSServer
```

---

## Plugin 내부 모듈 구조

```mermaid
graph TD
    %% Entry Point
    Module[FPoorforceModule<br/>모듈 진입점]

    %% Group by logical layer
    subgraph UI["🪟 UI Layer"]
        Interceptor[FAssetEditorInterceptor<br/>에디터 이벤트 hook]
        CBExt[FPoorforceContentBrowserExtension<br/>우클릭 Sync 메뉴]
        Dialogs[Dialogs<br/>상태 및 진행률 팝업]
    end

    subgraph Workflow["🔁 Workflow Layer"]
        WF[FLockWorkflow<br/>락 흐름 컨트롤러]
    end

    subgraph Core["🧩 Core (Data & Config)"]
        Config[PoorforceConfig<br/>JSON 로더]
        UserId[PoorforceUserId<br/>git email / 폴백]
        Resolver[PoorforcePathResolver<br/>경로 → 락 키]
    end

    subgraph External["🌐 External Services"]
        LockClient[FLockServerClient<br/>Upstash REST]
        GitLfs[PoorforceGitLfs<br/>git lfs lock]
        Rclone[FRcloneProcessManager<br/>rclone copyto]
        Watcher[FDetachedWatcherSpawner<br/>PS 워처]
        Discord[PoorforceDiscord<br/>webhook 알림]
    end

    %% Connections
    Module -.->|Init| UI
    Module -.->|Init| WF

    UI <-->|이벤트 / 상태 연동| WF
    
    WF -->|설정 및 데이터 참조| Core
    WF -->|API 및 툴 실행| External
```

---

## 락 라이프사이클

### LockOnly 흐름

```mermaid
flowchart TD
    %% 상태 정의 (캡슐: 프로세스의 주요 상태 기점)
    Free(["🟢 Free (해제)"])
    Blocked(["🔴 Blocked (잠김)"])
    Owned(["🔵 Owned (내 점유)"])
    Kept(["🟡 Kept (병합 대기)"])

    %% 1. 에셋 접근 시도
    Free --> TryOpen{"더블클릭"}
    TryOpen -->|"성공"| Owned
    TryOpen -->|"타인 점유"| Blocked

    %% 2. Blocked 상태 (취소/복귀 흐름: 점선)
    Blocked --> Abort["닫기 (포기)"]
    Abort -.->|"원상 복구"| Free

    %% 3. Owned 상태 분기 (작업 및 닫기)
    Owned --> Save["저장 (LFS Lock)"]
    Save -.->|"상태 유지"| Owned

    Owned --> Close{"에셋 닫기"}
    Close -.->|"변경 없음"| Free
    Close -->|"변경 있음"| Kept

    %% 4. Kept 상태 최종 해제 흐름
    Kept --> Resolve["CI 머지 / 수동 / TTL"]
    Resolve -->|"최종 해제"| Free
```

### LockAndSync 흐름

```mermaid
flowchart TD
    %% 노드 스타일 정의 (시작/종료는 캡슐, 조건은 마름모, 작업은 사각형)
    Start(["더블클릭 (시작)"])
    EndAbort(["닫힘 (취소)"])
    EndSuccess(["끝 (완료)"])

    Start --> Acquire{"락 획득 (SET NX)"}

    %% 1. 락 획득 분기
    Acquire -->|"성공"| Spawn["Watcher 실행"]
    Acquire -->|"실패"| CheckOwner{"점유자 확인"}

    %% 2. 점유자 확인 및 예외 처리
    CheckOwner -->|"내 ID"| Refresh["TTL 갱신"]
    CheckOwner -->|"다른 사람"| Blocked["차단 다이얼로그"]

    Blocked --> ForceUnlock{"강제 해제?"}
    ForceUnlock -.->|"아니오"| EndAbort
    ForceUnlock -->|"예"| Discord["Discord 알림"]
    Discord -->|"재시도"| Acquire

    %% 3. 동기화 검증 (rclone check)
    Spawn --> Check{"rclone check"}
    Check -->|"Same (동일)"| Open(["에디터 열림"])
    Check -->|"Differ (다름)"| CancelLock["락 해제"]
    
    CancelLock --> SyncDialog["Sync 필요 안내"]
    SyncDialog -.-> EndAbort

    Refresh --> Open

    %% 4. 에디터 작업 및 종료 플로우
    Open --> Work["사용자 작업"]
    Work --> Close{"에셋 닫기"}

    Close -->|"변경 있음"| Upload["rclone upload"]
    Close -->|"변경 없음"| Release["락 해제"]

    Upload --> Release
    Release -.-> EndSuccess
```

---

## 협업 시나리오

### A 가 작업한 후 B 가 받기

```mermaid
sequenceDiagram
    actor A as 개발자 A
    participant Redis
    participant Drive
    actor B as 개발자 B

    A->>Redis: SET NX foo
    Redis-->>A: OK (Case 1)
    A->>Drive: rclone check
    Drive-->>A: same
    A->>A: 에디터 열림 + 작업 + 저장 + 닫기
    A->>Drive: rclone copyto (v2 업로드)
    A->>Redis: DEL foo

    Note over A,B: A 의 락 풀림

    B->>Redis: SET NX foo
    Redis-->>B: OK
    B->>Drive: rclone check
    Drive-->>B: differ (B 로컬 v1 ≠ Drive v2)
    B->>Redis: DEL foo (락 풀기)
    B->>B: "Sync 필요" 다이얼로그

    Note over B: 사용자가 우클릭 → Sync

    B->>B: close + UnloadPackages
    B->>Drive: rclone copyto (v2 다운로드)
    Drive-->>B: v2
    B->>B: 다시 더블클릭
    B->>Redis: SET NX foo
    B->>Drive: rclone check (이번엔 same)
    B->>B: 에디터 열림 (v2)
```

### A 가 크래시 — 워처 자동 복구

```mermaid
sequenceDiagram
    actor A
    participant Editor as Unreal Editor (A)
    participant Watcher as Detached Watcher<br/>(PowerShell)
    participant Redis
    participant Drive

    A->>Editor: 더블클릭
    Editor->>Redis: SET NX foo
    Editor->>Watcher: spawn (PID, sentinel path)
    Editor->>Drive: rclone check (same)
    Editor->>A: 열림
    A->>Editor: 작업 + 저장

    Note over Editor: ★ CRASH ★

    loop 2초마다
        Watcher->>Watcher: 부모 PID 살아있나?
    end

    Watcher->>Watcher: 부모 죽음 감지<br/>sentinel 파일 없음
    Watcher->>Redis: GET foo
    Redis-->>Watcher: <A의 ID>|<timestamp>
    Watcher->>Watcher: 내 락 맞음
    Watcher->>Drive: rclone copyto (저장된 v2 업로드)
    Watcher->>Redis: DEL foo
    Watcher->>Watcher: exit (성공)
```

### CI 가 PR 머지 시 락 자동 해제

```mermaid
sequenceDiagram
    actor Dev
    participant GitHub
    participant Actions as GitHub Actions
    participant Scripts as release-*.sh
    participant Redis
    participant LFS as GitHub LFS

    Dev->>GitHub: PR open + push
    Note over GitHub: Dev 가 작업 중<br/>(Redis & LFS 락 살아있음)
    Dev->>GitHub: PR merge to develop

    GitHub->>Actions: pull_request closed (merged=true)
    Actions->>Actions: git diff origin/develop...HEAD<br/>(변경된 .uasset 추출)
    Actions->>Scripts: release-locks.sh keys.txt
    Scripts->>Redis: DEL gy:asset:Foo<br/>DEL gy:asset:Bar
    Actions->>Scripts: release-lfs-locks.sh paths.txt
    Scripts->>LFS: git lfs unlock --force Foo.uasset<br/>git lfs unlock --force Bar.uasset

    Note over Redis,LFS: 모든 락 해제 완료<br/>팀원들이 다시 작업 가능
```

---

## 안전망 계층

여러 계층의 fail-safe 로 락이 영구히 잠기지 않도록:

```mermaid
flowchart LR
    A[정상 종료] --> B[Plugin 이 DEL]
    C[크래시] --> D[Detached Watcher 가<br/>자동 업로드 + DEL]
    E[Watcher 도 죽음<br/>or PowerShell 미설치] --> F[Redis TTL 만료<br/>= 7d / 3d]
    G[유저가 깜빡한 락] --> H[콘솔 Poorforce.ReleaseLock]
    I[PR 머지] --> J[GitHub Actions<br/>자동 DEL + LFS unlock]
```

| 계층 | 시점 | 동작 |
|------|------|------|
| 1. 정상 종료 | 에셋 닫기 | Plugin 이 즉시 DEL |
| 2. 크래시 복구 | 에디터 강제 종료 후 | Detached Watcher 가 자동 업로드 + DEL |
| 3. TTL 만료 | 모든 위 단계 실패 시 | Redis 가 자동 만료 (`LockOnlyTtl`, `LockAndSyncTtl`) |
| 4. 수동 해제 | 사용자 의도 | 콘솔 `Poorforce.ReleaseLock <key>` |
| 5. CI 자동 해제 | PR 머지 | `release-locks.sh` / `release-lfs-locks.sh` |

---

## 참고사항

- **Windows 전용** (PowerShell 의존)
- **Upstash 토큰이 디스크에 평문 저장** (PoorforceConfig.json + 임시 워처 스크립트). git 에 안 올라가도록 `.gitignore` 필수
- **Content Browser 락 오버레이 없음** (Redis 호출 비용 문제로 보류)
- **워처가 죽으면 락 영구 유지** → TTL 이 최후 안전망
- **에셋 삭제 처리 없음** — Pre/PostDelete 훅 미구현. LockAndSync 삭제 시 업로드 실패 다이얼로그 뜨고 리모트 파일 안 지워짐. 일단 삭제 피하기
- **LockAndSync 자동 메모리 갱신 없음** — 더블클릭 시 자동 다운로드 안 함. 다른 사람이 업로드한 거 받으려면 명시적으로 우클릭 → Poorforce → Sync 필요
- **맵(.umap) 락 처리 미지원* — World 에셋은 main viewport 에 직접 로드되므로:
  - **Content Browser 더블클릭** 만 락 hook 발화. Redis 락 + LFS 락 정상 동작
  - **default map 자동 로드 / File > Open Level / LoadMap 호출** 은 hook 없음 → 락 안 잡힘
  - 락 충돌 다이얼로그가 떠도 맵 자체는 이미 로드돼서 차단 못 함
  - 시스템 정합성은 유지 (락 없으니 LFS lock 도 안 시도 → 다른 사람 변경 안 덮어씀)
  - **운영 규약 권장**: default map 은 수정하지 않는 placeholder 로 두고, 작업할 맵은 항상 Content Browser 에서 명시적으로 열기
