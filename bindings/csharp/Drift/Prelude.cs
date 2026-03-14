namespace Drift
{
    public static class Prelude
    {
        public static drift.Vec2 Vec2(float x, float y) => new(x, y);
        public static drift.Rect Rect(float x, float y, float w, float h) => new(x, y, w, h);
        public static drift.Color Color(byte r, byte g, byte b, byte a = 255) => new(r, g, b, a);
        public static drift.ColorF ColorF(float r, float g, float b, float a = 1f) => new(r, g, b, a);
    }
}
