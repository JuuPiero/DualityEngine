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

- **File menu** (phía trên) -- Open Project, Save/Load Scene, Build for 3DS.
- **Hierarchy** (bên trái) -- mọi entity trong scene. Bấm vào một entity để chọn;
  **Create Entity** tạo một entity trống mới.
- **Properties** (bên phải) -- mọi component của entity đang chọn, với các field
  có thể chỉnh sửa trực tiếp. **+ Add Component** ở dưới cùng thêm component mới;
  nút "..." trên các component không bắt buộc sẽ xóa chúng.
- **Scene** / **Game** (ở giữa, dạng tab) -- **Scene** chia thành hai pane cạnh
  nhau (màn Top/Bottom), mỗi pane là một camera editor tự do, độc lập với
  camera thật trong scene. Mỗi pane có riêng nút **2D**/**3D** để chuyển cách
  hiển thị/điều khiển (không đụng đến `CameraComponent::Projection` thật của
  screen đó -- xem mục 4 bên dưới về pane 3D). Ở chế độ 2D: kéo chuột giữa để
  pan, lăn con lăn để zoom, bấm vào sprite để chọn, các nút Translate/Rotate/
  Scale chuyển chế độ gizmo. **Game** hiển thị chính xác những gì camera trong
  scene thực render -- tức hình ảnh bạn thực sự sẽ thấy trên máy console.
- **Console** / **Content Browser** (phía dưới, dạng tab) -- log output và trình
  duyệt thư mục `Assets/` của project hiện tại. Kéo asset vào field texture trong
  Properties để gán; kéo file từ Explorer vào để import.

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
2. **Tạo Material.** Hiện tại engine chưa có nút "Create Material" trong
   Content Browser -- tạo thủ công một file `<tên>.material.json` trong
   `Assets/` (ví dụ `Assets/Materials/Red.material.json`) với nội dung:
   ```json
   { "Color": { "r": 0.9, "g": 0.2, "b": 0.2, "a": 1.0 }, "Texture": "" }
   ```
   `Texture` là GUID của một ảnh (rỗng = màu phẳng theo `Color`). Sau khi lưu
   file, mở lại Content Browser (hoặc đợi lần Refresh kế tiếp) để nó nhận GUID,
   rồi kéo file này vào field **Material** của Mesh Renderer.
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
   nắm gizmo, **chuột phải kéo** xoay camera quanh điểm nhìn (orbit), **chuột
   giữa kéo** để pan, **lăn chuột** để zoom. Camera này độc lập với
   `CameraComponent` thật -- pan/orbit/zoom ở đây không đụng đến gameplay.
6. **Gizmo 3D.** Cùng ba nút Translate/Rotate/Scale ở trên dùng chung với pane
   2D, chỉ khác là có thêm trục Z (xanh dương) bên cạnh X (đỏ)/Y (xanh lá).
   Field Transform trong Properties luôn chỉnh được trực tiếp như một cách
   thay thế.

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
bao quát toàn bộ API bên dưới trong một script -- nên đọc từ đầu đến cuối một lần
sau khi đã nắm được phần cơ bản. Script này được gắn vào cả entity "ApiShowcase"
(2D, sprite) lẫn "TestCube3D" (3D, mesh) trong scene mẫu -- cùng một class,
tự phát hiện `GetEntity().HasComponent<T>()` để chạy đúng nhánh 2D hay 3D (di
chuyển theo mặt phẳng X/Y hay X/Z, xoay quanh trục Z hay Y), minh họa cách một
`Behaviour` không nên giả định trước hình dạng component của entity mình gắn vào.

## 7. Scripting API

Tất cả API dưới đây đều được gọi bên trong `OnCreate`/`OnUpdate` (hoặc bất kỳ
method nào) của một lớp con của `Behaviour`.

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
cho mọi âm thanh cần chạy trên thiết bị.

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

**Chưa dùng được từ script** (xem `ROADMAP.md`): `Duality::Log` -- hiện chưa có
tương đương `Debug.Log` để gửi log từ `GameScripts` đến panel Console.

## 8. Chạy thử trên phần cứng thật

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
`carstene1ns/3ds-bannertool`) rồi đặt vào `Tools/` một lần.

## Tiếp theo nên xem gì

- `README.md` -- kiến trúc, cấu trúc repository và danh sách đầy đủ các giới hạn
  đã biết.
- `ROADMAP.md` -- mọi thứ đang được lên kế hoạch nhưng chưa xây dựng (UI system,
  prefab, Project Hub và nhiều thứ khác).
