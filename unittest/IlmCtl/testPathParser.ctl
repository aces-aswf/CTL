// Fixture for testPathParser: functions whose arguments exercise nested
// types that TypeStorage::set/get must address via multi-segment "/" paths.

namespace pathParser
{

struct WithArr
{
    float v[3];
    int n;
};

struct Point
{
    float x;
    float y;
};

void
fnMat3(output float m[3][3])
{
    for (int i = 0; i < 3; i = i + 1)
        for (int j = 0; j < 3; j = j + 1)
            m[i][j] = 0.0;
}

void
fnStructWithArr(output WithArr w)
{
    w.v[0] = 0.0;
    w.v[1] = 0.0;
    w.v[2] = 0.0;
    w.n    = 0;
}

void
fnArrOfStruct(output Point arr[4])
{
    for (int i = 0; i < 4; i = i + 1)
    {
        arr[i].x = 0.0;
        arr[i].y = 0.0;
    }
}

} // namespace pathParser
