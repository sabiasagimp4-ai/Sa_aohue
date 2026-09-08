// Shared with the native regression test. Seven 7-bit rows, center at (3,3).
// A component fitting in <=3 x <=3 pixels is wholly visible from any member.
// A component reaching the window boundary is always retained.
bool IsSmallComponent(uint occupied[7], int maxSize)
{
    if (maxSize <= 0 || (occupied[3] & 8u) == 0u) return false;
    uint reached[7] = {0u, 0u, 0u, 8u, 0u, 0u, 0u};
    uint next[7];
    for (int step = 0; step < 48; ++step)
    {
        bool changed = false;
        for (int y = 0; y < 7; ++y)
        {
            uint adjacent = reached[y];
            if (y > 0) adjacent |= reached[y - 1];
            if (y < 6) adjacent |= reached[y + 1];
            next[y] = (adjacent | (adjacent << 1) | (adjacent >> 1)) & occupied[y];
            changed = changed || next[y] != reached[y];
        }
        for (int j = 0; j < 7; ++j) reached[j] = next[j];
        if (!changed) break;
    }
    if (reached[0] != 0u || reached[6] != 0u) return false;
    uint columns = 0u;
    int top = 7, bottom = -1;
    for (int row = 0; row < 7; ++row)
    {
        columns |= reached[row];
        if (reached[row] != 0u)
        {
            if (row < top) top = row;
            bottom = row;
        }
    }
    if ((columns & 65u) != 0u) return false;
    int left = 7, right = -1;
    for (int x = 0; x < 7; ++x)
        if ((columns & (1u << x)) != 0u)
        {
            if (x < left) left = x;
            right = x;
        }
    return right - left + 1 <= maxSize && bottom - top + 1 <= maxSize;
}
