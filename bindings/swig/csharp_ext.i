// =============================================================================
// csharp_ext.i - C# operator overloads and language-specific extensions
// =============================================================================
// Adds idiomatic C# operator overloads (+, -, *), equality, hashing, and
// ToString() for Drift value types. These are injected into the generated
// C# proxy classes via %typemap(cscode).
// =============================================================================

// ---------------------------------------------------------------------------
// Vec2: arithmetic operators, equality, hashing, ToString
// ---------------------------------------------------------------------------
%typemap(cscode) drift::Vec2 %{
  // Arithmetic operators
  public static Vec2 operator +(Vec2 a, Vec2 b) {
    return new Vec2(a.x + b.x, a.y + b.y);
  }
  public static Vec2 operator -(Vec2 a, Vec2 b) {
    return new Vec2(a.x - b.x, a.y - b.y);
  }
  public static Vec2 operator *(Vec2 v, float s) {
    return new Vec2(v.x * s, v.y * s);
  }
  public static Vec2 operator *(float s, Vec2 v) {
    return new Vec2(v.x * s, v.y * s);
  }
  public static Vec2 operator -(Vec2 v) {
    return new Vec2(-v.x, -v.y);
  }

  // Equality
  public override bool Equals(object obj) {
    if (obj is Vec2 other)
      return x == other.x && y == other.y;
    return false;
  }
  public override int GetHashCode() {
    return x.GetHashCode() ^ (y.GetHashCode() << 16);
  }
  public static bool operator ==(Vec2 a, Vec2 b) {
    if (ReferenceEquals(a, b)) return true;
    if (a is null || b is null) return false;
    return a.x == b.x && a.y == b.y;
  }
  public static bool operator !=(Vec2 a, Vec2 b) {
    return !(a == b);
  }

  // Conversion to/from System.Numerics.Vector2
  public System.Numerics.Vector2 ToNumerics() {
    return new System.Numerics.Vector2(x, y);
  }
  public static Vec2 FromNumerics(System.Numerics.Vector2 v) {
    return new Vec2(v.X, v.Y);
  }
%}

// ---------------------------------------------------------------------------
// Rect: equality, hashing, ToString
// ---------------------------------------------------------------------------
%typemap(cscode) drift::Rect %{
  public override bool Equals(object obj) {
    if (obj is Rect other)
      return x == other.x && y == other.y && w == other.w && h == other.h;
    return false;
  }
  public override int GetHashCode() {
    return x.GetHashCode() ^ (y.GetHashCode() << 8) ^
           (w.GetHashCode() << 16) ^ (h.GetHashCode() << 24);
  }
  public static bool operator ==(Rect a, Rect b) {
    if (ReferenceEquals(a, b)) return true;
    if (a is null || b is null) return false;
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
  }
  public static bool operator !=(Rect a, Rect b) {
    return !(a == b);
  }
%}

// ---------------------------------------------------------------------------
// Color: equality, hashing, ToString, implicit conversion to/from ColorF
// ---------------------------------------------------------------------------
%typemap(cscode) drift::Color %{
  public override bool Equals(object obj) {
    if (obj is Color other)
      return r == other.r && g == other.g && b == other.b && a == other.a;
    return false;
  }
  public override int GetHashCode() {
    return (r << 24) | (g << 16) | (b << 8) | a;
  }
  public static bool operator ==(Color a, Color b) {
    if (ReferenceEquals(a, b)) return true;
    if (a is null || b is null) return false;
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
  }
  public static bool operator !=(Color a, Color b) {
    return !(a == b);
  }

  /// Convert Color (0-255) to ColorF (0.0-1.0)
  public ColorF ToColorF() {
    return new ColorF(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
  }
%}

// ---------------------------------------------------------------------------
// ColorF: equality, hashing, ToString
// ---------------------------------------------------------------------------
%typemap(cscode) drift::ColorF %{
  public override bool Equals(object obj) {
    if (obj is ColorF other)
      return r == other.r && g == other.g && b == other.b && a == other.a;
    return false;
  }
  public override int GetHashCode() {
    return r.GetHashCode() ^ (g.GetHashCode() << 8) ^
           (b.GetHashCode() << 16) ^ (a.GetHashCode() << 24);
  }
  public static bool operator ==(ColorF a, ColorF b) {
    if (ReferenceEquals(a, b)) return true;
    if (a is null || b is null) return false;
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
  }
  public static bool operator !=(ColorF a, ColorF b) {
    return !(a == b);
  }

  /// Convert ColorF (0.0-1.0) to Color (0-255)
  public Color ToColor() {
    return new Color(
      (byte)(r * 255.0f + 0.5f),
      (byte)(g * 255.0f + 0.5f),
      (byte)(b * 255.0f + 0.5f),
      (byte)(a * 255.0f + 0.5f));
  }
%}

// ---------------------------------------------------------------------------
// Handle<T>: equality, hashing, ToString, implicit bool conversion
// These apply to all template instantiations (TextureHandle, etc.)
// ---------------------------------------------------------------------------
%define HANDLE_CSHARP_CODE(HANDLE_TYPE)
%typemap(cscode) drift::Handle<drift::HANDLE_TYPE> %{
  public override bool Equals(object obj) {
    var other = obj as $csclassname;
    if (other == null) return false;
    return id == other.id;
  }
  public override int GetHashCode() {
    return (int)id;
  }
  public static bool operator ==($csclassname a, $csclassname b) {
    if (ReferenceEquals(a, b)) return true;
    if (a is null || b is null) return false;
    return a.id == b.id;
  }
  public static bool operator !=($csclassname a, $csclassname b) {
    return !(a == b);
  }

  /// Implicit conversion to bool (true if handle is valid).
  public static implicit operator bool($csclassname h) {
    return h != null && h.valid();
  }
%}
%enddef

HANDLE_CSHARP_CODE(TextureTag)
HANDLE_CSHARP_CODE(SpritesheetTag)
HANDLE_CSHARP_CODE(AnimationTag)
HANDLE_CSHARP_CODE(SoundTag)
HANDLE_CSHARP_CODE(PlayingSoundTag)
HANDLE_CSHARP_CODE(FontTag)
HANDLE_CSHARP_CODE(TilemapTag)
HANDLE_CSHARP_CODE(EmitterTag)
HANDLE_CSHARP_CODE(CameraTag)

// ---------------------------------------------------------------------------
// Config: ToString
// ---------------------------------------------------------------------------
%typemap(cscode) drift::Config %{
  public override string ToString() {
    return $"Config(\"{title}\", {width}x{height})";
  }
%}

// ---------------------------------------------------------------------------
// Transform2D: ToString
// ---------------------------------------------------------------------------
%typemap(cscode) drift::Transform2D %{
  public override string ToString() {
    return $"Transform2D(pos={position}, rot={rotation:F3}, scale={scale})";
  }
%}

// ---------------------------------------------------------------------------
// PhysicsBody / PhysicsShape: equality and ToString
// ---------------------------------------------------------------------------
%typemap(cscode) drift::PhysicsBody %{
  public override bool Equals(object obj) {
    if (obj is PhysicsBody other)
      return index == other.index && world == other.world && revision == other.revision;
    return false;
  }
  public override int GetHashCode() {
    return index ^ (world << 16) ^ (revision << 24);
  }
  public override string ToString() {
    return $"PhysicsBody(index={index}, world={world}, rev={revision})";
  }
%}

%typemap(cscode) drift::PhysicsShape %{
  public override bool Equals(object obj) {
    if (obj is PhysicsShape other)
      return index == other.index && world == other.world && revision == other.revision;
    return false;
  }
  public override int GetHashCode() {
    return index ^ (world << 16) ^ (revision << 24);
  }
  public override string ToString() {
    return $"PhysicsShape(index={index}, world={world}, rev={revision})";
  }
%}

// ---------------------------------------------------------------------------
// Sprite: ToString
// ---------------------------------------------------------------------------
%typemap(cscode) drift::Sprite %{
  public override string ToString() {
    return $"Sprite(zOrder={zOrder}, visible={visible})";
  }
%}

// ---------------------------------------------------------------------------
// SpriteEntry: ToString
// ---------------------------------------------------------------------------
%typemap(cscode) drift::SpriteEntry %{
  public override string ToString() {
    return $"SpriteEntry({transform}, {sprite})";
  }
%}

// ---------------------------------------------------------------------------
// EntityCommands: C# property for entity ID
// ---------------------------------------------------------------------------
%typemap(cscode) drift::EntityCommands %{
  public ulong Id {
    get { return id(); }
  }
%}

// ---------------------------------------------------------------------------
// EntityBuilder: return self for chaining in C#
// ---------------------------------------------------------------------------
%typemap(cscode) drift::EntityBuilder %{
  // C# property wrapper for the entity ID
  public ulong Id {
    get { return id(); }
  }
%}

// ---------------------------------------------------------------------------
// Script: C# property accessors for built-in components
// ---------------------------------------------------------------------------
%typemap(cscode) drift::Script %{
  public Transform2D TransformComponent {
    get { return getTransformMut(); }
  }
  public Sprite SpriteComponent {
    get { return getSpriteMut(); }
  }
%}
