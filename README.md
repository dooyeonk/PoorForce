# Poorforce

학생/소규모 팀용 "Poor man's perforce" — Unreal Editor 에디터에서 동시 편집 충돌을 방지하는 Redis 기반 락 시스템 + (선택) Google Drive 동기화.

**대상**: GitHub Free LFS (락 미지원) 같은 환경에서 Perforce/Plastic 없이 협업 잠금만 필요할 때.

## 기능

| 모드 | 락 | 파일 저장 | 용도 |
|------|----|---------|------|
| `LockOnly` | Redis | Git LFS (사용자 책임) | 일반 무료 에셋 (git 으로 관리) |
| `LockAndSync` | Redis | Google Drive + Rclone (자동) | 유료 에셋 (git 제외) |

## 설치

1. 플러그인을 `<프로젝트>/Plugins/Poorforce/` 에 배치
2. Editor 타겟에서 빌드 (5.7 기준)
3. 아래 설정 파일 작성

### 의존성

- **Upstash Redis** 무료 계정 (REST API, apn1 권장)
- **rclone** (LockAndSync 모드 사용 시): https://rclone.org/
- **PowerShell** (Windows 기본) — 디태치드 워처가 사용

## 설정

`<프로젝트루트>/PoorforceConfig.json` 작성. **반드시 `.gitignore` 에 추가** (Upstash 토큰 포함).

```json
{
  "UpstashUrl": "https://apn1-xxxxx.upstash.io",
  "UpstashToken": "AYZ_xxxxxxxxxxxxxxxxxxxxxxx",
  "LockKeyNamespace": "gy",
  "RcloneExecutable": "rclone",

  "LockOnlyTtl": "7d",
  "LockAndSyncTtl": "3d",

  "ManagedPaths": [
    {
      "ContentPath": "/Game/ThirdParty/PaidAssets/",
      "Mode": "LockAndSync",
      "RcloneRemote": "gdrive:GY_PaidAssets"
    },
    {
      "ContentPath": "/Game/GY/",
      "Mode": "LockOnly"
    }
  ],

  "DiscordWebhookUrl": ""
}
```

| 필드 | 설명 |
|------|------|
| `UpstashUrl` / `UpstashToken` | Upstash Redis REST 인증 |
| `LockKeyNamespace` | Redis 키 prefix (다중 프로젝트 격리용). 단일 프로젝트면 빈 문자열 가능 |
| `RcloneExecutable` | rclone 실행 파일. 기본 `"rclone"` (PATH) |
| `LockOnlyTtl` | LockOnly 락 TTL. 형식: `7d`, `12h`, `90m`, `3d12h`. 기본 `7d` |
| `LockAndSyncTtl` | LockAndSync 락 TTL. 기본 `3d` |
| `ManagedPaths` | 관리 대상 콘텐츠 prefix + 모드 + (LockAndSync 시) 리모트 |
| `DiscordWebhookUrl` | (선택) 강제 해제 시 알림 |

## 동작

### LockOnly 모드 (예: `Content/GY/`)

플러그인은 **락만** 담당. 파일은 사용자가 git LFS 로 직접 관리.

```
열기      → 락 획득 → 작업
저장      → (자동)
닫기      → 락 유지 (PR 머지 전까지)
push      → PR 생성 → 리뷰 → 머지
머지 후   → 락 해제 (콘솔 커맨드 OR CI 워크플로우)
```

### LockAndSync 모드 (예: `Content/ThirdParty/PaidAssets/`)

플러그인이 전부 자동:

```
열기      → 락 획득 → rclone 다운로드 → 에디터 재오픈
작업/저장 → (자동)
닫기      → rclone 업로드 → 락 해제
```

## 콘솔 커맨드

에디터 콘솔(틸드 키 `~`) 또는 Output Log:

- `Poorforce.ListLocks` — 내가 보유 중인 락 목록
- `Poorforce.ReleaseLock <key>` — 특정 락 수동 해제

## CI 워크플로우 — PR 머지 시 자동 해제

PR 머지 시 변경된 에셋의 락을 자동 해제하려면 CI에서 `scripts/release-locks.sh` 호출.

### GitHub Actions 샘플 (`.github/workflows/release-poorforce-locks.yml`)

```yaml
name: Release Poorforce locks

on:
  pull_request:
    types: [closed]
    branches: [main]

jobs:
  release:
    if: github.event.pull_request.merged == true
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          fetch-depth: 0

      - name: Compute lock keys from changed files
        id: keys
        run: |
          # Content/GY/Foo/Bar.uasset -> gy:asset:Foo/Bar
          git diff --name-only origin/main...HEAD \
            | grep -E '^Content/GY/.*\.(uasset|umap)$' \
            | sed -E 's|^Content/GY/||; s|\.(uasset|umap)$||; s|^|gy:asset:|' \
            > keys.txt
          echo "count=$(wc -l < keys.txt)" >> "$GITHUB_OUTPUT"

      - name: Release locks
        if: steps.keys.outputs.count != '0'
        env:
          UPSTASH_URL: ${{ secrets.UPSTASH_URL }}
          UPSTASH_TOKEN: ${{ secrets.UPSTASH_TOKEN }}
        run: cat keys.txt | ./Plugins/Poorforce/scripts/release-locks.sh
```

**참고:**
- `Content/GY/` 와 `gy:asset:` prefix는 `PoorforceConfig.json` 의 `ContentPath` / `LockKeyNamespace` 와 매칭되어야 함
- Repo Secrets 에 `UPSTASH_URL`, `UPSTASH_TOKEN` 등록
- `Content/ThirdParty/PaidAssets/` (LockAndSync) 는 git 에 없으므로 워크플로우 대상 아님

## 트러블슈팅

### 락이 안 풀려요

1. 콘솔 `Poorforce.ListLocks` 로 본인이 가진 락 확인
2. `Poorforce.ReleaseLock <key>` 로 수동 해제
3. 그래도 안 되면 — TTL(7일 기본) 만료 대기, 또는 동료가 차단 다이얼로그에서 "강제 해제" 사용

### 다른 사람 락 강제 해제

차단 다이얼로그에서 `[강제 해제…]` 버튼. 2단계 확인 후 사유 입력 (선택). Discord webhook 설정돼 있으면 원 작업자에게 알림 전송.

### rclone 실행이 안 됨

- `rclone version` 으로 PATH 확인
- `RcloneExecutable` 설정으로 절대 경로 명시 가능
- `rclone config` 로 리모트 (예: `gdrive`) 인증

### 에디터 크래시 시

LockAndSync 모드에서는 디태치드 워처가 자동으로 업로드 + 락 해제 시도. 해당 락이 강제 해제됐던 거면 업로드 안 함 (덮어쓰기 방지).

## 알려진 한계

- **Windows 전용** (PowerShell 의존)
- **Upstash 토큰이 디스크에 평문 저장** (PoorforceConfig.json + 임시 워처 스크립트). git 에 안 올라가도록 `.gitignore` 필수
- **Content Browser 락 오버레이 없음** (Redis 호출 비용 문제로 보류)
- **워처가 죽으면 락 영구 유지** → TTL 이 최후 안전망
