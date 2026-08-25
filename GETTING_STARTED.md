# Bắt đầu với DualityEngine

Hướng dẫn thực hành: build Editor, làm quen với các panel, tạo một scene và viết
script đầu tiên. Để xem chi tiết về kiến trúc/hệ thống build và danh sách đầy đủ
các tính năng, hãy xem `README.md`; để xem những gì đang được lên kế hoạch nhưng
chưa xây dựng, hãy xem `ROADMAP.md`.

## 1. Build lần đầu

```
build.bat        REM desktop Editor -> build\DualityEditor\DualityEditor.exe
run.bat          REM khởi chạy Editor (thiết lập PATH để tìm glfw3.dll/glew32.dll)
```

Cần có devkitPro (với các package `3ds`/`citro2d`/`citro3d`) và bộ công cụ MSYS2
mingw64 đi kèm -- nếu đây là lần build đầu tiên trên máy mới, hãy xem mục
"Prerequisites" trong `README.md`. `build.bat` tự cấu hình lại nếu build cache
trông đã cũ, vì vậy bạn luôn có thể chạy lại lệnh này sau khi pull thay đổi.

## 2. Làm quen với Editor

Chạy `run.bat`. Bạn sẽ thấy:

- **File menu** (phía trên) -- Open Project, Save/Load Scene, **Save Scene As...**
  (lưu một *bản snapshot* của scene hiện tại ra file mới, không đổi scene mà
  Save/Load Scene đang thao tác -- đây là cách để có file scene thứ hai cho
  `SceneManager`/`Behaviour::LoadScene` nhắm tới, xem mục 8 và 10 bên dưới),
  **Build for 3DS**, và **Build for PC** (build lại `DualityPlayerDesktop`
  trong build tree desktop có sẵn -- không cần bước cook asset nào vì target
  đó tự đọc thẳng `Assets/` của project lúc chạy; sau khi build xong, chạy
  bằng `run-desktop-player.bat`). Hai nút build này dùng chung một cờ trạng
  thái nên không chạy đồng thời được.
- **Hierarchy** (bên trái) -- mọi entity trong scene. Bấm vào một entity để chọn;
  **Create Entity** tạo một entity trống mới. Có ô tìm kiếm: gõ vào để lọc ra
  danh sách phẳng mọi entity khớp tên trong toàn scene (cây/kéo-thả trở lại khi
  xóa ô tìm kiếm). Chuột phải vào một entity để **Create Child Entity** hoặc
  **Create Prefab from Selection** (ghi ra `Assets/Prefabs/<Tên>.prefab`);
  kéo một Prefab asset từ Content Browser thả vào khoảng trống của panel này để
  tạo bản sao (instantiate) vào scene.
- **Properties** (bên phải) -- mọi component của entity đang chọn, với các field
  có thể chỉnh sửa trực tiếp. Mọi entity đều có checkbox **Active** (tương đương
  `GameObject.SetActive` của Unity) cạnh Transform/Name/Tag. **+ Add Component**
  ở dưới cùng thêm component mới; nút "..." trên các component không bắt buộc sẽ
  xóa chúng. Checkbox **Lock** ở trên cùng ghim panel vào entity/asset đang hiện,
  bỏ qua mọi lần chọn khác ở Hierarchy/Scene/Content Browser cho tới khi bỏ khóa
  -- bật nó lên trước khi kéo một Material/Texture từ Content Browser vào field
  `AssetRef` ở đây, nếu không click-để-bắt-đầu-kéo sẽ vô tình đổi luôn nội dung
  panel đang hiện.
- **Scene** / **Game** (ở giữa, dạng tab) -- **Scene** chia thành hai pane cạnh
  nhau (màn Top/Bottom), mỗi pane là một camera editor tự do, độc lập với
  camera thật trong scene. Mỗi pane có riêng nút **2D**/**3D** để chuyển cách
  hiển thị/điều khiển (không đụng đến `CameraComponent::Projection` thật của
  screen đó -- xem mục 4 bên dưới về pane 3D). Ở chế độ 2D: kéo chuột giữa để
  pan, lăn con lăn để zoom, bấm vào sprite để chọn, các nút Translate/Rotate/
  Scale chuyển chế độ gizmo. Camera entity vẽ hẳn khung nhìn thật (frustum --
  mặt phẳng near/far, FOV) thay vì chỉ một điểm đánh dấu, nên bạn thấy chính
  xác camera đang nhìn về đâu. **Game** hiển thị chính xác những gì camera trong
  scene thực render -- tức hình ảnh bạn thực sự sẽ thấy trên máy console.
- **Console** (phía dưới, dạng tab) -- log output (`Duality::Log`, kể cả
  `Behaviour::LogInfo/LogWarn/LogError` gọi từ script), có màu theo cấp độ
  (Trace/Info/Warn/Error), checkbox lọc theo cấp độ, ô tìm kiếm theo nội dung,
  và nút Clear.
- **Content Browser** (phía dưới, dạng tab) -- trình duyệt thư mục `Assets/`
  của project hiện tại, có ô tìm kiếm lọc file theo tên trong thư mục đang mở
  (thư mục vẫn luôn hiện để còn điều hướng được). Kéo asset vào field
  `AssetRef` trong Properties (texture, Material, Prefab, ...) để gán; kéo file
  từ Explorer vào để import. **Click một lần vào file để chọn** -- Properties
  hiện đúng loại Inspector theo phần đuôi file: `.mat` (Material, edit đầy đủ),
  `.asset` (ScriptableObject, edit đầy đủ), `.prefab`/`.scene` (tóm tắt
  read-only: số entity, tên root/entity gốc), ảnh (`.png`/`.jpg`/...) và
  `.wav` hiện ra **Import Settings** kiểu Unity/Cocos (Filter Mode/Wrap
  Mode/Mipmap cho texture, Volume cho audio -- lưu vào file `.meta` của asset
  đó, không đụng tới file gốc). Chuột phải vào khoảng trống để **Create ->
  Material** hoặc **Create -> ScriptableObject -> TênClass** (mục 8).

Bấm **Play** (phía trên panel Game) để chạy scene trực tiếp -- physics và script
bắt đầu hoạt động. **Stop** đưa scene về trạng thái edit (chính xác hơn là về
trạng thái hiện có trong bộ nhớ -- hiện chưa có snapshot tự động để hoàn tác về
trước lúc Play, nên hãy save trước nếu muốn giữ các thay đổi).

## 3. Scene đầu tiên

1. **Hierarchy -> Create Entity.** Một entity trống mới xuất hiện và được chọn
   sẵn.
2. **Properties -> + Add Component -> Sprite Renderer.** Entity sẽ hiển thị một
   hình vuông trắng nhỏ tại vị trí do Transform xác định.
3. Kéo một ảnh từ **Content Browser** vào field **Texture** của Sprite Renderer
   trong Properties để tạo hình ảnh thật. (Nếu `Assets/` chưa có gì, trước tiên
   hãy kéo một file `.png`/`.jpg` từ Windows Explorer vào -- file được copy vào
   thư mục mà Content Browser đang hiển thị.)
4. Chuyển sang chế độ xem **Scene**, chọn entity và kéo các tay nắm gizmo (các nút
   Translate/Rotate/Scale ở trên cùng chọn thao tác) để di chuyển/xoay/đổi kích
   thước -- hoặc chỉnh trực tiếp các field `Transform` trong Properties.
5. **+ Add Component -> Box Collider 2D** (hoặc **Circle Collider 2D**) cùng với
   **Rigidbody 2D** sẽ biến entity thành một vật thể physics -- bấm Play để xem nó
   phản ứng với trọng lực. Collider hiện dưới dạng đường viền màu xanh lá trong
   Scene để bạn thấy phạm vi thật của chúng khi chỉnh sửa.
6. Chọn **File -> Save Scene** khi bạn đã hài lòng.

Mọi field editor, mọi component và các menu Add/Remove Component đều hoàn toàn
generic (dựa trên reflection) -- khi thêm một loại component mới vào engine,
không cần nối thêm logic riêng cho editor.

## 4. Scene 3D đầu tiên

Pipeline 3D dựng trực tiếp trên devkitPro/citro3d (không qua citro2d) ở phía
3DS và OpenGL hiện đại (VAO/VBO + shader) ở phía desktop -- unlit hoàn toàn
(chưa có lighting/shadow, xem `ROADMAP.md`). Một screen (Top hoặc Bottom) chỉ
render qua đúng một pipeline mỗi frame, 2D hoặc 3D, không bao giờ chồng cả hai.

1. **Properties -> + Add Component -> Mesh Renderer.** Entity hiển thị một
   khối lập phương trắng (mặc định `Primitive = Cube`; có thể đổi sang `Sphere`
   hoặc `Plane`). Kích thước thật lấy từ `Transform -> Scale`, không có field
   `Size` riêng như Sprite Renderer.
2. **Tạo Material.** Trong Content Browser, chuột phải vào khoảng trống ->
   **Create -> Material** -- tạo ngay `NewMaterial.mat` (màu trắng, không
   texture) và tự đăng ký GUID. Click chọn file đó, Properties hiện ra field
   **Color** và **Texture** để chỉnh trực tiếp (tự động lưu xuống đĩa mỗi khi
   một field thay đổi, giống hệt component/ScriptableObject) -- kéo một ảnh từ
   Content Browser vào field Texture nếu muốn dùng ảnh thay vì màu phẳng. Sau
   đó kéo file `.mat` này vào field **Material** của Mesh Renderer.
3. **Import mesh riêng (tùy chọn).** Field **Mesh** của Mesh Renderer nhận một
   file `.obj` (Wavefront) kéo từ Content Browser -- khi được gán, nó thay thế
   hẳn `Primitive`. Parser tự viết, không phụ thuộc thư viện ngoài (để chắc
   chắn build được cho devkitARM): chỉ đọc vị trí/texcoord/mặt (tam giác hoặc
   đa giác lồi, tự động tam giác hóa), không đọc normal/material trong file,
   không hỗ trợ chỉ số âm (relative index). FBX/glTF chưa được hỗ trợ (xem
   `ROADMAP.md` để biết lý do chọn OBJ).
4. **Camera 3D.** Chọn entity Camera của screen tương ứng, đổi
   `Camera -> Projection` từ `Orthographic` sang `Perspective` -- lúc này panel
   **Game** của đúng screen đó sẽ render qua pipeline 3D thật, dùng
   `Fov Degrees`/`Near Plane`/`Far Plane` thay vì `Zoom`.
5. **Xem/điều khiển ở Scene view.** Bấm nút **3D** trên pane tương ứng. Điều
   khiển chuột theo phong cách Unity: **chuột trái** chọn mesh hoặc kéo tay
   nắm gizmo, **chuột phải kéo** xoay camera quanh điểm nhìn (orbit -- dùng
   quaternion thật, không giới hạn ±89° kiểu gimbal lock, nên kéo liên tục sẽ
   lật qua đỉnh/đáy mượt mà như Unity/Blender), **chuột giữa kéo** để pan,
   **lăn chuột** để zoom. Camera này độc lập với `CameraComponent` thật --
   pan/orbit/zoom ở đây không đụng đến gameplay.
6. **Gizmo 3D.** Cùng ba nút Translate/Rotate/Scale ở trên dùng chung với pane
   2D, chỉ khác là có thêm trục Z (xanh dương) bên cạnh X (đỏ)/Y (xanh lá).
   Field Transform trong Properties luôn chỉnh được trực tiếp như một cách
   thay thế.
7. **Vật lý 3D (tùy chọn).** Giống hệt bước physics ở mục 3 nhưng cho 3D:
   **+ Add Component -> Rigidbody 3D** cùng với **Box Collider 3D** hoặc
   **Sphere Collider 3D** biến mesh thành vật thể vật lý thật (dựng trên
   Bullet Physics) -- bấm Play để xem nó rơi theo trọng lực và va chạm với
   các mesh khác cũng có Rigidbody 3D. Bật **Is Trigger** trên collider nếu
   chỉ muốn phát hiện chạm mà không cần phản ứng vật lý (dùng cho vùng nhặt
   đồ, vùng kích hoạt -- xem `OnTriggerEnter` ở mục 7).

## 5. Tạo (hoặc mở) project

Editor luôn có một project đang mở -- ở lần khởi chạy đầu tiên, nó tự động tạo
`SampleProject/` cạnh file executable. **File -> Open Project...** mở trình duyệt
project để chọn file `.dproj`; khi mở project khác, scene đang dùng sẽ được thay
đổi, Play sẽ dừng nếu đang chạy, và Content Browser sẽ trỏ đến thư mục `Assets/`
của project đó.

Hiện chưa có wizard "New Project" (xem `ROADMAP.md`) -- để tự tạo project thứ hai,
hãy copy cấu trúc thư mục của `SampleProject/` (`Assets/` + file `.dproj`) sang vị
trí mới rồi mở file `.dproj` đó.

## 6. Viết script đầu tiên

Script nằm trong `GameScripts/` và kế thừa từ `Duality::Behaviour` -- C++ thuần,
không dùng ngôn ngữ script nhúng, được build thành DLL có thể hot-reload cho chế
độ Play-in-Editor trên desktop và được link tĩnh trực tiếp vào bản build 3DS
(cùng một bộ source, không cần thay đổi code giữa hai nền tảng).

1. **Tạo header**, `GameScripts/Include/MyBehaviour.h`:
   ```cpp
   #pragma once
   #include "DualityEngine/Scene/Behaviour.h"

   class MyBehaviour : public Duality::Behaviour {
   public:
       void OnCreate() override;
       void OnUpdate(float deltaTime) override;
   };
   ```
2. **Tạo source**, `GameScripts/Source/MyBehaviour.cpp`:
   ```cpp
   #include "MyBehaviour.h"
   #include "DualityEngine/Scene/Components.h"
   #include "ScriptRegistration.h"

   void MyBehaviour::OnCreate() {
       // Chạy một lần, ngay khi Play bắt đầu (hoặc khi entity được tạo
       // trong lúc game đang chạy).
   }

   void MyBehaviour::OnUpdate(float deltaTime) {
       auto& transform = GetComponent<Duality::TransformComponent>();
       transform.Translation.x += 50.0f * deltaTime; // trôi sang phải với tốc độ 50 units/giây
   }

   REGISTER_BEHAVIOUR(MyBehaviour)
   ```
3. **Thêm cả hai file vào `GameScripts/CMakeLists.txt`** (đặt
   `Source/MyBehaviour.cpp` cạnh các entry hiện có -- header không cần liệt kê,
   chỉ cần source).
4. Trong Editor, chọn một entity, **+ Add Component -> Behaviour**, rồi nhập
   `MyBehaviour` vào field **Class** (được tìm theo tên lúc Play, giống việc chọn
   một MonoBehaviour trong Unity -- không bind lúc compile).
5. Bấm **Reload Scripts** (trên toolbar của panel Game) để build lại `GameScripts`
   và hot-load vào Editor đang chạy -- không cần khởi động lại. Từ giờ, sửa
   `MyBehaviour.cpp` rồi bấm Reload Scripts là toàn bộ vòng lặp phát triển.

`GameScripts/Source/ApiShowcaseBehaviour.cpp` là một ví dụ hoàn chỉnh có thể chạy,
bao quát các API Input/Audio/Save/DateTime trong một script -- nên đọc từ đầu
đến cuối một lần sau khi đã nắm được phần cơ bản. Script này được gắn vào cả
entity "ApiShowcase" (2D, sprite) lẫn "TestCube3D" (3D, mesh) trong scene mẫu --
cùng một class, tự phát hiện `GetEntity().HasComponent<T>()` để chạy đúng nhánh
2D hay 3D (di chuyển theo mặt phẳng X/Y hay X/Z, xoay quanh trục Z hay Y), minh
họa cách một `Behaviour` không nên giả định trước hình dạng component của
entity mình gắn vào. `GameScripts/Source/CollisionLogBehaviour.cpp` là ví dụ
tương tự cho vòng đời va chạm/trigger (mục dưới) -- gắn vào một entity có
Rigidbody + collider bất kỳ rồi xem panel Console khi nó chạm vào thứ khác.

## 7. Vòng đời (Lifecycle)

- `OnCreate()` -- chạy một lần khi Play bắt đầu (giống `Awake` của Unity, chạy
  kể cả khi entity bắt đầu ở trạng thái inactive).
- `OnEnable()` / `OnDisable()` -- chạy mỗi khi `Scene::IsEffectivelyActive` của
  entity này đổi trạng thái (do chính nó gọi `SetActive`, hoặc do cha nó đổi) --
  có thể chạy nhiều lần trong một lần Play. `OnUpdate` đơn giản là không được
  gọi trong lúc inactive.
- `OnUpdate(float deltaTime)` -- mỗi frame, khi đang active.
- `OnCollisionEnter(Entity other)` / `OnCollisionExit(Entity other)` -- chạy
  cho CẢ HAI phía của một cặp đang chạm nhau (quy ước của Unity) khi không bên
  nào là trigger. Hoạt động y hệt dù `other` là entity 2D hay 3D.
- `OnTriggerEnter(Entity other)` / `OnTriggerExit(Entity other)` -- giống trên,
  nhưng cho cặp có ít nhất một bên bật `IsTrigger`.
- `OnDestroy()` -- chạy một lần lúc Stop (một instance vẫn đang enable sẽ được
  gọi thêm một `OnDisable()` cuối ngay trước đó).

## 8. Scripting API

Tất cả API dưới đây đều được gọi bên trong bất kỳ method vòng đời nào (mục 7)
của một lớp con của `Behaviour`.

**Entity và component** -- `GetComponent<T>()` (add/has/remove không được expose
trực tiếp trên `Behaviour`; hãy truy cập `GetEntity()` để dùng các thao tác đó:
`GetEntity().AddComponent<T>()`/`GetEntity().HasComponent<T>()` và các thao tác
tương tự):
```cpp
auto& transform = GetComponent<Duality::TransformComponent>();
transform.Rotation.z += 90.0f * deltaTime; // độ, không phải radian

// Cùng một script dùng được cho cả entity 2D lẫn 3D -- kiểm tra component trước
// khi giả định hình dạng của entity (xem ApiShowcaseBehaviour.cpp để có ví dụ đầy đủ).
if (GetEntity().HasComponent<Duality::MeshRendererComponent>())
    transform.Rotation.y += 90.0f * deltaTime; // mesh: xoay quanh Y (yaw)
else
    transform.Rotation.z += 90.0f * deltaTime; // sprite: xoay quanh Z
```

**Input** (phong cách Unity Input Manager cũ -- `Duality::KeyCode` hỗ trợ các phím
desktop phổ biến `W`/`A`/`S`/`D`/mũi tên/`Space`/`Enter`/`Escape` và các nút 3DS
`GamepadA`/`B`/`X`/`Y`/`L`/`R`/`GamepadStart`/`Select`/D-Pad; keycode không có ý
nghĩa trên nền tảng hiện tại sẽ luôn được đọc là chưa nhấn):
```cpp
if (GetKeyDown(Duality::KeyCode::Space)) { /* chỉ chạy một lần ở frame được nhấn */ }
if (GetKey(Duality::KeyCode::GamepadA)) { /* true ở mọi frame đang giữ nút */ }

float h = GetAxis("Horizontal"); // +-1 dạng digital trên desktop (WASD/mũi tên), real
float v = GetAxis("Vertical");   // analog trên 3DS (Circle Pad) -- cùng code, cả hai nền tảng

if (GetPointerDown()) {
    glm::vec2 p = GetPointerPosition(); // chuột trên desktop, cảm ứng trên 3DS
}
```

**Trạng thái Active** -- tương đương `GameObject.SetActive`/`activeSelf` của Unity:
```cpp
SetActive(false); // tắt entity này -- OnDisable() sẽ chạy, OnUpdate() ngừng chạy
bool active = IsActive();
```
Bật/tắt lúc đang Play KHÔNG tự thêm/xóa physics body của Rigidbody -- chỉ ảnh
hưởng việc body có tồn tại hay không tại thời điểm Play bắt đầu.

**Log** -- tương đương `Debug.Log` của Unity, hiện ra ngay trong panel Console
của Editor:
```cpp
LogInfo("Player đã chạm đất");
LogWarn("Hết đạn");
LogError("Không tìm thấy save file");
```

**Chuyển scene** -- tương đương `SceneManager.LoadScene` của Unity:
```cpp
LoadScene("Scenes/Level2.scene"); // đường dẫn tương đối so với Assets/ của project,
                                  // tạo bằng File -> Save Scene As... (mục 2)
```
Đây là yêu cầu hoãn lại (deferred) -- việc đổi scene thật sự diễn ra giữa hai
frame, không phải ngay khi hàm này return, vì một script không thể an toàn phá
hủy chính cái Scene mà lời gọi của nó đang chạy bên trong.

**Prefab** -- tương đương `Object.Instantiate` của Unity:
```cpp
Duality::Entity spawned = Instantiate("<prefab-asset-guid>");
```
Tạo một bản sao mới của asset `.prefab` (xem "Create Prefab from
Selection" ở mục 2) làm entity gốc (root) mới trong scene của chính script này.

**ScriptableObject** -- tương đương `ScriptableObject` của Unity: một asset dữ
liệu dùng lại được (`.asset`), không gắn vào entity nào cả -- ví dụ một
"GameSettings" mà nhiều script cùng đọc, chỉnh một chỗ trong Properties là mọi
nơi đọc thấy ngay, thay vì chép số liệu vào field của từng entity:
```cpp
#include "GameSettingsData.h" // class do chính bạn định nghĩa, xem bên dưới

GameSettingsData* settings = LoadScriptableObject<GameSettingsData>("<asset-guid>");
if (settings)
    score += settings->ScorePerCoin;
```
Trả về `nullptr` nếu guid rỗng/không resolve được, hoặc GameScripts chưa được
(re)load -- xử lý giống hệt một `AssetRef` chưa gán ở bất kỳ chỗ nào khác trong
engine. Để định nghĩa một loại `ScriptableObject` mới:

1. Kế thừa `Duality::ScriptableObject`, khai báo field bình thường, và thêm
   `static std::vector<Duality::FieldHandle> Fields()` liệt kê chúng qua
   `MakeField()` (giống hệt cách `Reflection.cpp` khai báo field cho các
   component có sẵn) -- xem `GameScripts/Include/GameSettingsData.h` để có ví
   dụ đầy đủ.
2. Đăng ký bằng `REGISTER_SCRIPTABLE_OBJECT(TênClass)` ở cuối file `.cpp`
   (thay vì `REGISTER_BEHAVIOUR`).
3. Thêm file nguồn mới vào `GameScripts/CMakeLists.txt`.
4. Trong Content Browser, chuột phải vào khoảng trống -> **Create ->
   ScriptableObject -> TênClass** để tạo một file `.asset` mới với giá
   trị mặc định; click chọn file đó để chỉnh field ngay trong Properties
   (tự động lưu xuống đĩa mỗi khi một field thay đổi).

`GameScripts/Include/GameSettingsData.h` là ví dụ có sẵn
(`PlayerSpeed`/`ScorePerCoin`/`GameTitle`), và
`SampleProject/Assets/GameSettings.asset` là một instance thật của nó --
mở project mẫu, chọn file này trong Content Browser để xem field hiện ra
trong Properties.

**Âm thanh** -- phát asset WAV bằng GUID của Content Browser (kéo asset vào một
field `AssetRef` trước để xem GUID được resolve, hoặc tham chiếu đến GUID của một
field `AssetRef` hiện có):
```cpp
PlaySound("<asset-guid>");             // phát một lần
PlaySound("<asset-guid>", true);       // lặp lại (ví dụ: nhạc nền)
StopAllSounds();
```
Desktop phát được mọi định dạng mà miniaudio giải mã được; bản build 3DS chỉ hỗ
trợ WAV PCM 16-bit (không có decoder đi kèm trên phần cứng thật) -- hãy dùng WAV
cho mọi âm thanh cần chạy trên thiết bị. Mỗi lần `PlaySound` chạy, nó tự đọc
field **Volume** (0..1) trong Import Settings của chính file `.wav` đó (chọn
file trong Content Browser để chỉnh) và áp dụng ngay -- không cần tham số
volume riêng trong code.

**Save/load** -- JSON thuần, không cần method `Behaviour` (gọi trực tiếp class):
```cpp
#include "DualityEngine/IO/SaveSystem.h"

nlohmann::json save = Duality::SaveSystem::LoadJson("Saves/mygame.json"); // {} nếu thiếu
int highScore = save.value("highScore", 0);
save["highScore"] = std::max(highScore, currentScore);
Duality::SaveSystem::SaveJson("Saves/mygame.json", save);
```

**Đồng hồ thời gian thực** -- cũng gọi trực tiếp, không cần method `Behaviour`:
```cpp
#include "DualityEngine/Core/DateTime.h"

Duality::DateTime now = Duality::DateTime::Now(); // UTC
// now.Year, .Month, .Day, .Hour, .Minute, .Second, .DayOfWeek (0=Sunday)
```

## 9. Chạy thử trên phần cứng thật

```
build-3ds.bat    REM clean configure+build -> build-3ds\DualityPlayer\DualityPlayer.3dsx / .cia
run-3ds.bat      REM khởi chạy file .3dsx trong Citra
```

Hoặc bấm **Build for 3DS** trong File menu của Editor -- thao tác này save scene
trước rồi chạy cùng quy trình build. `.3dsx` dùng cho Homebrew Launcher/`3dslink`;
`.cia` được cài bằng FBI trên phần cứng thật/CFW và cần `makerom.exe`/`bannertool.exe`
trong `Tools/` -- nếu thiếu, build vẫn thành công và chỉ bỏ qua `.cia` (kèm cảnh
báo CMake), chứ không tự tải chúng. Nếu cần output `.cia`, hãy tải cả hai từ
release chính thức trên GitHub (`3DSGuy/Project_CTR` và
`carstene1ns/3ds-bannertool`) rồi đặt vào `Tools/` một lần. Riêng
`tex3ds.exe` (dùng để cook texture PNG -> `.t3x` khi bấm Build for 3DS) được
Editor tự tìm (thử `DEVKITPRO`, rồi registry `devkitProUpdater`, rồi
`C:\devkitPro`) chứ không hard-code đường dẫn -- không cần cấu hình gì thêm.

## 10. Xây game hoàn chỉnh đầu tiên

Phần này ghép các mảnh đã học ở trên (di chuyển, va chạm, Active, UI, chuyển
scene) thành một vòng lặp gameplay thật, nhỏ nhưng đầy đủ: nhặt một đồng xu rồi
chuyển sang màn "Thắng". Dùng luôn scene mẫu có sẵn (`SampleProject/Assets/
Scene.scene`) thay vì tạo asset mới -- entity "ApiShowcase" đã di chuyển được
bằng WASD/Circle Pad (xem `ApiShowcaseBehaviour.cpp`), đó chính là "player" của
chúng ta.

1. **Tạo đồng xu.** Hierarchy -> Create Entity, đặt tên "Coin". + Add Component
   -> Sprite Renderer (chỉnh `Color` sang màu vàng cho dễ nhận), + Add Component
   -> Box Collider 2D, bật **Is Trigger**. Đặt `Transform -> Translation` gần
   vị trí bắt đầu của "ApiShowcase" (ví dụ lệch sang phải khoảng 60-80 unit) để
   dễ đi tới.

2. **Viết script nhặt coin**, `GameScripts/Include/CollectCoinBehaviour.h`:
   ```cpp
   #pragma once
   #include "DualityEngine/Scene/Behaviour.h"

   class CollectCoinBehaviour : public Duality::Behaviour {
   public:
       void OnTriggerEnter(Duality::Entity other) override;
   };
   ```
   và `GameScripts/Source/CollectCoinBehaviour.cpp`:
   ```cpp
   #include "CollectCoinBehaviour.h"
   #include "ScriptRegistration.h"

   void CollectCoinBehaviour::OnTriggerEnter(Duality::Entity other) {
       LogInfo("Đã nhặt coin!");
       SetActive(false);              // ẩn đồng xu đi
       LoadScene("Scenes/Win.scene");  // chuyển sang màn thắng (tạo ở bước 4)
   }

   REGISTER_BEHAVIOUR(CollectCoinBehaviour)
   ```
   Thêm cả hai file vào `GameScripts/CMakeLists.txt`, gán `CollectCoinBehaviour`
   vào field **Class** của một `Behaviour` component trên entity "Coin", bấm
   **Reload Scripts**.

3. **Thử nhặt coin.** Bấm Play, dùng WASD/mũi tên di chuyển "ApiShowcase" (thực
   ra `ApiShowcaseBehaviour` di chuyển theo input, và pointer nếu bạn giữ chuột/
   chạm) vào vị trí đồng xu. Panel Console sẽ hiện dòng "Đã nhặt coin!" ngay khi
   hai collider chạm nhau. Vì lúc này chưa có `Scenes/Win.scene`, `LoadScene` vẫn
   sẽ thực hiện việc chuyển scene (không crash) nhưng nạp vào một scene RỖNG
   (Deserialize thất bại chỉ ghi lỗi vào Console, không phục hồi lại scene cũ) --
   bấm Stop rồi **File -> Load Scene** để lấy lại scene gameplay ban đầu trước
   khi làm tiếp bước 4.

4. **Tạo màn "Thắng".** Bấm Stop. Xóa (hoặc tạm giấu) các entity gameplay không
   cần cho màn thắng nếu muốn scene này đơn giản, thêm một entity Sprite Renderer
   màu khác để biết rõ đang ở scene mới (ví dụ đặt tên "WinScreen"). **File ->
   Save Scene As...**, lưu vào `SampleProject/Assets/Scenes/Win.scene` (tạo thư
   mục `Scenes` ngay trong hộp thoại nếu chưa có). Scene đang mở KHÔNG đổi sau
   bước này -- **File -> Load Scene** để quay lại scene gameplay ban đầu trước
   khi tiếp tục.

5. **Chạy lại từ đầu.** Bấm Play, đi tới đồng xu -- lần này `LoadScene
   ("Scenes/Win.scene")` sẽ thật sự chuyển Editor sang màn "Thắng" ngay giữa lúc
   đang Play (không cần bấm Stop), đúng như một scene transition thật trong
   game. Cùng file `Scene.scene`/`Win.scene` này chạy y hệt trên `DualityPlayerDesktop`
   (bấm `run-desktop-player.bat`) và trên 3DS thật (miễn `Scenes/Win.scene` nằm
   trong `Assets/` để được đóng gói vào romfs -- xem mục 9).

Từ đây, những hướng mở rộng tự nhiên: nhiều đồng xu (đếm số lượng bằng một
field tĩnh hoặc một entity "GameManager" riêng), đọc số điểm mỗi đồng xu từ
một `ScriptableObject` dùng chung (mục 8) thay vì hardcode trong script, một
UI Text hiện điểm số (UI hiện chưa có widget Text, xem `ROADMAP.md`), hoặc
dùng `OnCollisionEnter` thay vì `OnTriggerEnter` cho một cơ chế "va vào kẻ
địch thì thua".

## Tiếp theo nên xem gì

- `README.md` -- kiến trúc, cấu trúc repository và danh sách đầy đủ các giới hạn
  đã biết, bao gồm cả `Tests/` (bộ test tự động cho logic engine) và CI.
- `ROADMAP.md` -- mọi thứ đang được lên kế hoạch nhưng chưa xây dựng
  (joints/raycast cho physics, OnCollisionStay/OnTriggerStay, Project Hub và
  nhiều thứ khác).
