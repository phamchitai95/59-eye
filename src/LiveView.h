#pragma once
#include "Config.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QVector>
#include <QWidget>

class QButtonGroup;
class VideoTile;

class LiveView : public QWidget {
    Q_OBJECT
public:
    explicit LiveView(QWidget *parent = nullptr);

    void assign(const ChannelRef &ref);   // dua kenh vao o dang chon
    void setLayoutCount(int n);           // 1, 4, 9, 16, 36
    void autoFill();
    void closeAll();
    void closeDisallowed(); // dong o co kenh bi tat / khong co quyen
    VideoTile *tile(int i) const;
    void onAudioToggle(VideoTile *t);

private:
    void relayout();
    void select(int i);
    void onDoubleClicked(VideoTile *t);
    bool wantMain(int i) const;
    void syncStreams();

    QVector<VideoTile *> m_tiles;
    QGridLayout *m_grid = nullptr;
    QCheckBox *m_mainOnZoom = nullptr;
    QButtonGroup *m_layoutGroup = nullptr;
    int m_count = 4;
    int m_selected = 0;
    int m_maximized = -1;
};
