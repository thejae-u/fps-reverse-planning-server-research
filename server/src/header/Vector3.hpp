#pragma once
#include <string>

struct Vector3
{
    float x;
    float y;
    float z;

    Vector3 operator+(const Vector3& other) const
    {
        return { x + other.x, y + other.y, z + other.z };
    }

    Vector3 operator*(float scalar) const
    {
        return { x * scalar, y * scalar, z * scalar };
    }
    
    Vector3 operator*(const Vector3& other) const
    {
        return { x * other.x, y * other.y, z * other.z };
    }

    Vector3 operator-(const Vector3& other) const
    {
        return { x - other.x, y - other.y, z - other.z };
    }

    Vector3& operator+=(const Vector3& other)
    {
        x += other.x;
        y += other.y;
        z += other.z;

        return *this;
    }

    bool operator==(const Vector3& other) const
    {
        return this->x == other.x && this->y == other.y && this->z == other.z;
    }

    bool operator!=(const Vector3& other) const
    {
        return this->x != other.x || this->y != other.y || this->z != other.z;
    }

    float magnitude() const
    {
        return std::sqrt(x * x + y * y + z * z);
    }

    Vector3 normalize() const
    {
        const float mag = this->magnitude();
        return mag > 0.0f ? Vector3{ x / mag, y / mag, z / mag } : Vector3(0, 0, 1);
    }

    float dot(const Vector3& other) const
    {
        return x * other.x + y * other.y + z * other.z;
    }

    std::string to_string() const
    {
        return "(" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ")";
    }

    Vector3()
        : x(0), y(0), z(0)
    {
    }

    Vector3(const float x, const float y, const float z)
        : x(x), y(y), z(z)
    {
    }
};