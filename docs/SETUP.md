# 설치 가이드

## 1. 플러그인 배치

```
<프로젝트루트>/Plugins/Poorforce/
```

이 경로로 plugin 폴더 복사 또는 clone.

## 2. Upstash Redis 생성

1. https://upstash.com 가입 (GitHub OAuth 가능)
2. Console → **Create Database**
   - Name: 자유롭게 (예: `poorforce-gy`)
   - Type: **Regional**
   - Region: **AP Northeast 1 (Tokyo)** (한국에서 가장 가까움)
   - Eviction: Off
3. 생성된 DB 클릭 → **REST API** 탭
4. 복사 (나중에 `PoorforceConfig.json` 에 입력):
   - `UPSTASH_REDIS_REST_URL` → `UpstashUrl`
   - `UPSTASH_REDIS_REST_TOKEN` → `UpstashToken`

## 3. rclone 설치 + Google Drive 설정 (LockAndSync 사용 시)

### 3-1. rclone 설치

1. https://rclone.org/downloads/ 에서 Windows zip 다운로드
2. 압축 풀어서 `rclone.exe` 를 PATH 걸린 폴더에 배치 (예: `C:\Tools\rclone\`)
3. 터미널에서 확인:
   ```bash
   rclone version
   ```

### 3-2. Google Drive 호스트 셋업 (한 명만)

팀에서 한 사람이 GDrive 폴더를 만들고 다른 사람들에게 공유:

1. `rclone config`
   - `n` (new remote) → name: `gdrive`
   - storage: `drive` (Google Drive)
   - `client_id`, `client_secret`: 빈 칸 (기본값)
   - scope: `1` (Full access)
   - root_folder_id: 빈 칸
   - auto config: `y` → 브라우저 열림 → Google 로그인 → 권한 허용
   - team drive: `n`
2. `rclone mkdir gdrive:GY_PaidAssets` (또는 원하는 폴더 이름)
3. Google Drive 웹에서 `GY_PaidAssets` 폴더 우클릭 → "공유" → 팀원 이메일 추가 (편집자)

### 3-3. 팀원 (공유 받은 사람) 추가 설정

각 팀원 자기 PC 에서:

1. `rclone config` → 위와 같은 절차로 본인 Google 계정 인증
2. **공유 폴더 모드 활성화**:
   ```bash
   rclone config update gdrive shared_with_me true
   ```
3. 확인:
   ```bash
   rclone lsd gdrive:
   # GY_PaidAssets 폴더 보여야 함
   ```

## 4. Git LFS 설정 (LockOnly 사용 시)

LockOnly 모드 에셋은 git LFS 로 관리. 저장 시 자동으로 `git lfs lock` 호출됨.

```bash
git lfs install     # 한 번만 (전역 hook 설치)
git lfs locks       # 현재 락 목록 (에러 안 나면 셋업 OK)
```

`.gitattributes` 에 `lockable` 속성 명시:
```
*.uasset filter=lfs diff=lfs merge=lfs -text lockable
*.umap   filter=lfs diff=lfs merge=lfs -text lockable
```

## 5. PoorforceConfig.json 작성

**중요: `.gitignore` 에 반드시 추가** (Upstash 토큰 포함).

`<프로젝트루트>/PoorforceConfig.json`:

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

### 필드 설명

| 필드 | 설명 |
|------|------|
| `UpstashUrl` / `UpstashToken` | Upstash Redis REST 인증 |
| `LockKeyNamespace` | Redis 키 prefix (다중 프로젝트 격리용). 단일 프로젝트면 빈 문자열 가능 |
| `RcloneExecutable` | rclone 실행 파일. 기본 `"rclone"` (PATH) |
| `LockOnlyTtl` | LockOnly 락 TTL. 형식: `7d`, `12h`, `90m`, `3d12h`. 기본 `7d` |
| `LockAndSyncTtl` | LockAndSync 락 TTL. 기본 `3d` |
| `ManagedPaths` | 관리 대상 콘텐츠 prefix + 모드 + (LockAndSync 시) 리모트 |
| `DiscordWebhookUrl` | (선택) 강제 해제 시 알림 |

## 6. `.uproject` 에 plugin 등록 (선택)

자동 발견 되지만 명시적 등록 권장:

```json
"Plugins": [
    ...,
    {
        "Name": "Poorforce",
        "Enabled": true,
        "TargetAllowList": [ "Editor" ]
    }
]
```

## 7. Editor 빌드

Visual Studio / Rider 에서 Editor target 으로 빌드. 또는 첫 에디터 켤 때 "빌드 필요" 다이얼로그에서 빌드.

## 8. 동작 확인

에디터 켜고 Output Log → `LogPoorforce` 카테고리 필터:

```
LogPoorforce: Poorforce starting up...
LogPoorforce: Config loaded: 2 managed path(s), namespace='gy'
LogPoorforce: UserId resolved from git: <본인 email>
LogPoorforce: Poorforce ready. UserId='...', ManagedPaths=2, Rclone='rclone'
```

이 4-5줄 보이면 OK.
