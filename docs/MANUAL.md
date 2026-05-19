# 사용 매뉴얼

## 콘솔 커맨드

에디터 콘솔(틸드 키 `~`) 또는 Output Log:

- `Poorforce.ListLocks` — 내가 보유 중인 락 목록
- `Poorforce.ReleaseLock <key>` — 특정 락 수동 해제

## Content Browser 우클릭 메뉴

LockAndSync 모드 에셋에서 우클릭 → **Poorforce → Sync (download)**:
- close all editors for asset
- 메모리 unload
- rclone copyto (최신 다운로드)
- 사용자가 다음에 더블클릭하면 fresh 로드됨

LockAndSync 에셋에 "Sync 필요" 다이얼로그가 뜨면 이 메뉴로 받기.

## CI 워크플로우 — PR 머지 시 자동 해제

PR 머지 시 변경된 에셋의 락을 자동 해제하려면 CI에서 `scripts/release-locks.sh` 호출.

### GitHub Actions 샘플 (`.github/workflows/release-poorforce-locks.yml`)

```yaml
name: Release Poorforce locks

on:
  pull_request:
    types: [closed]
    branches: [main]

permissions:
  contents: write   # git lfs unlock API 호출에 필요

jobs:
  release:
    if: github.event.pull_request.merged == true
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          # 머지 커밋을 직접 체크아웃 (PR closed 이벤트에서 refs/pull/N/merge 가 stale 일 수 있어 base/merge SHA 로 명시)
          ref: ${{ github.event.pull_request.merge_commit_sha }}
          fetch-depth: 0
          lfs: true

      - name: Compute lock keys from changed files
        id: keys
        run: |
          # main 브랜치(base) 기준으로 이번 PR 머지 커밋에서 변경된 파일만 추출
          # Content/GY/Foo/Bar.uasset -> gy:asset:Foo/Bar
          git diff --name-only \
            ${{ github.event.pull_request.base.sha }} \
            ${{ github.event.pull_request.merge_commit_sha }} \
            | grep -E '^Content/GY/.*\.(uasset|umap)$' \
            | sed -E 's|^Content/GY/||; s|\.(uasset|umap)$||; s|^|gy:asset:|' \
            > keys.txt
          echo "count=$(wc -l < keys.txt)" >> "$GITHUB_OUTPUT"

      - name: Release Redis locks
        if: steps.keys.outputs.count != '0'
        env:
          UPSTASH_URL: ${{ secrets.UPSTASH_URL }}
          UPSTASH_TOKEN: ${{ secrets.UPSTASH_TOKEN }}
        run: cat keys.txt | ./Plugins/Poorforce/scripts/release-locks.sh

      - name: Compute LFS paths from changed files
        id: lfspaths
        run: |
          # 위와 동일하게 main 기준 PR 머지 커밋 diff 사용
          git diff --name-only \
            ${{ github.event.pull_request.base.sha }} \
            ${{ github.event.pull_request.merge_commit_sha }} \
            | grep -E '^Content/GY/.*\.(uasset|umap)$' \
            > lfs_paths.txt
          echo "count=$(wc -l < lfs_paths.txt)" >> "$GITHUB_OUTPUT"

      - name: Release LFS locks
        if: steps.lfspaths.outputs.count != '0'
        run: cat lfs_paths.txt | ./Plugins/Poorforce/scripts/release-lfs-locks.sh
```

**참고:**
- **기준 브랜치는 `main`** — 위 워크플로우는 `branches: [main]` 으로 필터링하고, diff 도 `pull_request.base.sha` (= PR 의 base 브랜치인 main 의 머지 직전 SHA) ↔ `pull_request.merge_commit_sha` (실제 머지 커밋) 을 비교한다. 다른 브랜치 (예: `develop`) 로 머지되는 PR 에도 락을 풀고 싶으면 `branches` 와 트리거 조건을 함께 조정할 것.
- `origin/main...HEAD` 같은 ref 비교는 PR closed 이벤트에서 `refs/pull/N/merge` 가 stale 이거나 main 과 동일 트리가 되어 빈 diff 가 나올 수 있어 사용하지 않음.
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

(LockOnly 모드는 LFS 권한 이슈로 강제 해제 버튼 안 보임 — LockAndSync 만 가능)

### CI 에서 LFS unlock 권한 거부

- 워크플로우의 `permissions: contents: write` 명시 확인
- 스크립트 내부 `git lfs unlock --force` 옵션이 들어가있는지 확인 (락 owner != actor 케이스 위해)
- repo 의 Branch protection rule에서 LFS lock 관련 권한 제한이 없는지 확인

### rclone 실행이 안 됨

- `rclone version` 으로 PATH 확인
- `RcloneExecutable` 설정으로 절대 경로 명시 가능
- `rclone config` 로 리모트 (예: `gdrive`) 인증

### 에디터 크래시 시

LockAndSync 모드에서는 디태치드 워처가 자동으로 업로드 + 락 해제 시도. 해당 락이 강제 해제됐던 거면 업로드 안 함 (덮어쓰기 방지).

### "다른 사용자가 이 에셋을 업데이트했습니다" 다이얼로그 뜨면

LockAndSync 에셋을 열려고 했는데 로컬 디스크와 드라이브가 달라서 차단된 상태. 안내대로:

1. Content Browser 에서 그 에셋을 **우클릭 → Poorforce → Sync (download)** 선택
2. notification "Sync 완료" 뜨면
3. 그 에셋 더블클릭 → 최신 버전으로 열림
