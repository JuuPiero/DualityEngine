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
  bằng `run-desktop-player.bat`), và **Build Settings...** (một cửa sổ riêng
  liệt kê danh sách scene sẽ đóng gói vào bản build 3DS, xem bên dưới). Hai
  nút build (3DS/PC) dùng chung một cờ trạng thái nên không chạy đồng thời
  được.
- **Build Settings...** (menu File) -- danh sách **Scenes In Build** (kéo một
  dòng để sắp xếp lại thứ tự, nút "x" để xóa; scene trên cùng là Start Scene)
  cùng danh sách **Other Scenes In Project** quét tự động mọi file `.scene`
  chưa có trong danh sách trên (click vào để thêm), và ngay trong cửa sổ này
  có nút **Build for 3DS** + thanh tiến trình. Hành vi thật đứng sau danh
  sách này: một khi đã thêm ít nhất một scene, CHỈ những scene được liệt kê
  mới được đóng gói vào bản build 3DS (danh sách rỗng = giữ nguyên hành vi
  cũ, đóng gói mọi scene tìm thấy).
- **Edit menu** (phía trên) -- **Project Settings...** (hiện Name/Assets
  Directory/Scripts Directory của project đang mở, chỉ xem, chưa chỉnh sửa
  được) và **Preferences...** (chọn file `.exe` của một editor ngoài, ví dụ
  VS Code, qua nút **Browse...**, rồi **Open Project in External Editor** sẽ
  mở project bằng editor đó -- lưu theo máy, không theo project, tại
  `%APPDATA%\DualityEngine\EditorSettings.json`).
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
  xác camera đang nhìn về đâu; field **Background** (màu) của Camera trong
  Properties chính là màu mà màn hình đó thực sự dùng để clear khi có camera.
  **Game** hiển thị chính xác những gì camera trong
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
  Folder / Material / ScriptableObject -> TênClass** (mục 8); chuột phải vào
  một file/folder có sẵn để **Rename** (label biến thành ô nhập, Enter hoặc
  click ra ngoài để lưu, Esc để hủy) hoặc **Delete** (có hỏi xác nhận trước,
  không có Recycle Bin). Kéo một file thả vào icon folder để di chuyển nó vào
  đó -- file `.meta` đi theo, GUID vẫn trỏ đúng nên không gì cần sửa lại.

Bấm **Play** (phía trên panel Game) để chạy scene trực tiếp -- physics và script
bắt đầu hoạt động. **Stop** đưa scene về ĐÚNG trạng thái ngay trước khi bấm Play
(script sửa Transform, kết quả physics, entity được Instantiate/destroy lúc Play
-- tất cả đều bị hoàn tác, giống hệt Unity) nhờ một snapshot chụp tự động ngay
lúc bấm Play, nên không cần save trước khi Play để "giữ chỗ" nữa.

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
   phản ứng với trọng lực. Khi entity đang được chọn, collider hiện dưới dạng
   đường viền màu xanh lá trong Scene để bạn thấy phạm vi thật của nó; bật
   checkbox **Edit** ngay trên component đó trong Properties để kéo tay nắm
   resize trực tiếp trong viewport (chỉnh `Size`/`Radius`) thay vì gõ số tay.
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
`SampleProject/` cạnh file executable. **File -> New Project...** mở hộp thoại Save
(dùng chung với "Save Scene As...") để chọn vị trí + tên -- ví dụ chọn
`F:\Projects\MyGame.dproj` sẽ tạo `F:\Projects\MyGame\MyGame.dproj` +
`F:\Projects\MyGame\Assets\` (một thư mục project riêng chứa `.dproj` ở gốc, giống
cấu trúc `SampleProject/` hiện có, và giống cách Unity tạo `location/name/` khi New
Project). **File -> Open Project...** mở trình duyệt project để chọn file `.dproj`
có sẵn; khi mở/tạo project khác, scene đang dùng sẽ được thay đổi, Play sẽ dừng nếu
đang chạy, và Content Browser sẽ trỏ đến thư mục `Assets/` của project đó.

Chưa có Project Hub / danh sách project gần đây (xem `ROADMAP.md`) -- New/Open
Project chỉ là hộp thoại file thuần túy, không nhớ lịch sử.

## 6. Viết script đầu tiên

Script kế thừa từ `Duality::Behaviour` -- C++ thuần, không dùng ngôn ngữ script
nhúng, compile chung vào một module `GameScripts` build thành DLL có thể
hot-reload cho chế độ Play-in-Editor trên desktop, và link tĩnh trực tiếp vào
bản build 3DS (cùng một bộ source, không cần thay đổi code giữa hai nền tảng).

**Cách nhanh nhất (script của riêng project bạn)**: chuột phải trong Content
Browser (trong `Assets/Scripts/` hoặc bất kỳ đâu dưới `Assets/` của project) ->
**Create > Script**. Editor tự tạo sẵn một cặp `.h`/`.cpp` (`NewBehaviour`,
`NewBehaviour1`, ... nếu tên trống đã bị dùng -- khác với `"NewScene (1)"` của
Scene/Material, tên file phải là một identifier C++ hợp lệ vì nó cũng chính là
tên class) kèm sẵn `REGISTER_BEHAVIOUR`, đổi tên/class tùy ý rồi viết logic vào
`OnCreate`/`OnUpdate`. Không cần sửa `GameScripts/CMakeLists.txt` -- mọi file
`.cpp` trong `Assets/Scripts/` của project đang mở tự động được gộp vào lần
**Reload Scripts** tiếp theo.

**Cách thủ công (script dùng chung, đi kèm engine)**: hợp lý cho một script mẫu
muốn ship kèm engine (như `BounceBehaviour`) chứ không thuộc riêng project nào
-- tạo `GameScripts/Include/MyBehaviour.h` + `GameScripts/Source/MyBehaviour.cpp`
theo đúng khuôn `BounceBehaviour.h`/`.cpp`, rồi tự thêm dòng
`Source/MyBehaviour.cpp` vào `GameScripts/CMakeLists.txt`'s danh sách hiện có.

Dù theo cách nào:

1. Override các lifecycle method cần dùng (xem bên dưới).
2. Gọi `REGISTER_BEHAVIOUR(MyBehaviour)` ở cuối file `.cpp` (đã có sẵn nếu tạo
   qua Content Browser).
3. Bấm **Reload Scripts** (trên toolbar của panel Game) để build lại
   `GameScripts` và hot-load vào Editor đang chạy -- không cần khởi động lại.
   Chạy nền (nút đổi thành "Reload Scripts (reloading...)" và mờ đi lúc đang
   build), nên Editor không bị đứng trong lúc chờ build xong. Từ giờ, sửa
   `MyBehaviour.cpp` rồi bấm Reload Scripts là toàn bộ vòng lặp phát triển.
4. Chọn một entity, **+ Add Component -> Add Script**, rồi chọn `MyBehaviour`
   trong submenu (mọi class đã `REGISTER_BEHAVIOUR` đều tự hiện ở đây -- script
   của project hay script dùng chung đều như nhau, không phân biệt sau khi đã
   build). Một entity có thể gắn bao nhiêu script khác nhau tùy ý (hoặc gắn
   cùng một script nhiều lần) -- giống một GameObject trong Unity mang nhiều
   MonoBehaviour cùng lúc; mỗi script hiện thành một thẻ riêng trong
   Properties, với nút "..." > Remove Script của riêng nó.

### Field hiện ra ở Properties (Inspector-editable fields)

Field public của script cũng hiện ra ở panel Properties được, giống
`[SerializeField]` của Unity -- một attribute thật trên từng field, kiểu
`UPROPERTY()` của Unreal:

```cpp
class BounceBehaviour : public Duality::Behaviour {
public:
    DUALITY_PROPERTY() float Amplitude = 40.0f;
    DUALITY_PROPERTY() float Speed = 8.0f;
    DUALITY_PROPERTY() Duality::EntityRef Target;   // kéo một entity từ Hierarchy thả vào field này

    DUALITY_PROPERTIES_AUTO()
    ...
};
```

`DUALITY_PROPERTY()` đánh dấu một field; `DUALITY_PROPERTIES_AUTO()` (một dòng,
không tham số) khai báo method `Fields()` của class. Cả hai hoàn toàn tùy chọn --
script không dùng dòng nào thì không có field nào hiện ở Inspector, y như trước
khi tính năng này tồn tại. Phần *định nghĩa* thật của `Fields()` được sinh ra bởi
`GameScripts/CodeGen/generate_fields.py`, một bước chạy trước khi build (gắn vào
`GameScripts/CMakeLists.txt`) quét mọi header trong `GameScripts/Include/` tìm
marker `DUALITY_PROPERTY()` rồi xuất ra
`GameScripts/Generated/ScriptFields.generated.cpp` -- đúng cách Unreal Header
Tool và tầng scripting C# của Polyphase-Engine giải quyết bài toán này (một
macro gắn trên một field không có cách nào tự biết tên/kiểu của chính nó qua
preprocessor thuần, nên cần một bước code-generation thật sự quét source, không
có cách nào khác).

Không có kiểu field riêng cho "tham chiếu đến component khác" -- giữ một
`Duality::EntityRef` rồi gọi `ResolveEntityRef(ref).GetComponent<T>()` để lấy
đúng component cần trên entity đó. Sửa field lúc đang Edit (không Play) sẽ
lưu cùng scene; sửa lúc đang Play chỉ ảnh hưởng object đang sống và mất khi
bấm Stop . Một giới hạn: field `EntityRef` KHÔNG sống sót qua
một lần Save/Load scene -- nó luôn về "chưa gán" sau khi load lại, vì handle
thô của entity không ổn định qua một lần reload (xem mục "Known limitations"
trong README.md).

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

**Input** (`DualityEngine/Scripting/ScriptInput.h`):
```cpp
#include "DualityEngine/Scripting/ScriptInput.h"

if (Duality::ScriptInput::GetKeyDown(Duality::KeyCode::Space)) { /* một lần mỗi nhấn */ }
float h = Duality::ScriptInput::GetAxis("Horizontal");
if (Duality::ScriptInput::GetPointerDown()) {
    glm::vec2 p = Duality::ScriptInput::GetPointerPosition();
    Duality::Screen s = Duality::ScriptInput::GetPointerScreen(); // Bottom trên 3DS thật
}
```

**Raycast** (`DualityEngine/Scripting/ScriptPhysics3D.h`):
```cpp
#include "DualityEngine/Scripting/ScriptPhysics3D.h"
#include "DualityEngine/Scripting/ScriptDebug.h"

glm::vec3 origin, direction;
if (Duality::ScriptPhysics3D::ScreenPointToRay(Duality::Screen::Bottom,
        Duality::ScriptInput::GetPointerPosition(), origin, direction)) {
    Duality::RaycastHit3D hit = Duality::ScriptPhysics3D::Raycast(origin, direction, 2000.f);
    if (hit)
        Duality::ScriptDebug::LogInfo("Trung!");
}
```

**Log** (`DualityEngine/Scripting/ScriptDebug.h`):
```cpp
Duality::ScriptDebug::LogInfo("message");
```

**Trạng thái Active** (trên `Behaviour`):
```cpp
SetActive(false); // tắt entity này -- OnDisable() sẽ chạy, OnUpdate() ngừng chạy
bool active = IsActive();
```
Bật/tắt lúc đang Play KHÔNG tự thêm/xóa physics body của Rigidbody -- chỉ ảnh
hưởng việc body có tồn tại hay không tại thời điểm Play bắt đầu.

**Chuyển scene** (`DualityEngine/Scene/SceneManager.h`):
```cpp
#include "DualityEngine/Scene/SceneManager.h"
Duality::SceneManager::RequestLoadScene("Scenes/Level2.scene");
```

**Prefab** (`DualityEngine/Scripting/ScriptScene.h`):
```cpp
#include "DualityEngine/Scripting/ScriptScene.h"
Duality::Entity spawned = Duality::ScriptScene::Instantiate("<prefab-asset-guid>");
```

**ScriptableObject**:
```cpp
#include "GameSettingsData.h"
GameSettingsData* settings = Duality::ScriptScene::LoadScriptableObject<GameSettingsData>("<asset-guid>");
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

**Âm thanh** -- one-shot (`ScriptAudio`) hoặc `AudioSource` component + `GetAudioSource().Play()`:
```cpp
#include "DualityEngine/Scripting/ScriptAudio.h"
Duality::ScriptAudio::PlaySound("<asset-guid>", false);

// Hoặc sau khi thêm Audio Source component trong Editor:
GetAudioSource().Play();
```
Desktop: miniaudio; 3DS: `ndsp` (WAV PCM 16-bit, citro/libctru).

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
   Thêm cả hai file vào `GameScripts/CMakeLists.txt`, chọn entity "Coin",
   **+ Add Component -> Add Script -> CollectCoinBehaviour**, bấm **Reload
   Scripts**.

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
