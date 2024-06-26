#include "Object/bot.h"


Bot::Bot(Judge *j, QObject *parent) :
    QThread(parent),
    judge(j)
{
    token = QRandomGenerator::global()->bounded(1<<16);
}

Bot::~Bot()
{
    judge->log(Level::Debug, "Bot"+QString::number(token)+" destructed");
    // delete this;
}

void Bot::init()
{
    searchStartTime = 0;
    ItemVector().swap(chooseVec);
    dfsBoardTime = 0;
    eps = 0.03;
    alpha = 0.31, beta = 0.61;

    judge->log(Level::Debug, "Bot"+QString::number(token)+" initialized");
}

bool Bot::IsInBoard(int x, int y)
{
    return x >= 0 && x < CHESSBOARD_SIZE && y >= 0 && y < CHESSBOARD_SIZE;
}

int Bot::CurColor() // 当前落棋颜色
{
    return curPlayer == (1 ^ curPlayerBak) ? playerRole : -playerRole;
}

void Bot::readFromJudge() // 从 judge 类读入数据
{
    curPlayer = judge->curPlayer;
    curPlayerBak = curPlayer;
    playerRole = judge->playerRole;
    CHESSBOARD_SIZE = judge->CHESSBOARD_SIZE;
}

void Bot::dfsBoard(int x, int y)
{
    dfsVis[x][y] = dfsBoardTime;
    for(int i = 0; i < 4; i++)
    {
        int xx = x + dx[i], yy = y + dy[i]; // 枚举相邻点
        if(!IsInBoard(xx, yy))
            continue; // 判断边界条件
        if(!curBoard[xx][yy])
            isEmpty[xx][yy] = 1;
        else if(curBoard[xx][yy] == curBoard[x][y] && dfsVis[xx][yy] != dfsBoardTime)
            dfsBoard(xx, yy);
    }
}

bool Bot::checkBoard(int x, int y)
{
    if(curBoard[x][y]) return false;
    int Allspace = 0;
    dfsBoardTime ++;
    for(int i = 0; i < 4; i++)
    {
        int xx = x + dx[i], yy = y + dy[i]; // 枚举相邻点
        if(!IsInBoard(xx, yy))
            continue; // 判断边界条件
        if(!curBoard[xx][yy])
        {
            Allspace ++;
            continue;
        }
        else if(dfsVis[xx][yy] != dfsBoardTime)// 同色和异色的连通块
        {
            memset(isEmpty, 0, sizeof(isEmpty));
            dfsVis[x][y] = dfsBoardTime;
            isEmpty[x][y] = 1;
            dfsBoard(xx, yy);
            int space = 0;
            for(int x = 0; x < CHESSBOARD_SIZE; x++)
                for(int y = 0; y < CHESSBOARD_SIZE; y++)
                    space += isEmpty[x][y];
            if(curBoard[xx][yy] != CurColor() && space == 1)
                return 0;
            if(curBoard[xx][yy] == CurColor())
                Allspace += space - 1;
        }
    }
   //qDebug() << x << "," << y << "->" <<curPlayer << " " << CurColor() <<" " << Allspace <<  "\n";
    return Allspace > 0; // 最后落子得有气
}

double Bot::judgeBoard() // 估价函数判断当前局面
{
    int botValid = 0, rivalValid = 0, allPoint = 0;
    if(CurColor() == 1) rivalValid = 1; // 先手让一气
    else botValid = 1;
    for(int x = 0; x < CHESSBOARD_SIZE; x++)
        for(int y = 0; y < CHESSBOARD_SIZE; y++)
        {
            curPlayer = 0;
            bool botCan = checkBoard(x, y);
            curPlayer = 1;
            bool rivalCan = checkBoard(x, y);

            allPoint += (bool)curBoard[x][y];
            if(botCan ^ rivalCan) // 只有一人能落子
            {
                //qDebug() << x <<" " <<y;
                if(botCan)
                {
                    int isEye = 1;
                    for(int i = 0; i < 4; i++)
                        if(IsInBoard(x + dx[i], y + dy[i]))
                            isEye &= curBoard[x + dx[i]][y + dy[i]] == -CurColor();
                    botValid += 1 + isEye;
                }
                else
                {
                    int isEye = 1;
                    for(int i = 0; i < 4; i++)
                        if(IsInBoard(x + dx[i], y + dy[i]))
                            isEye &= curBoard[x + dx[i]][y + dy[i]] == CurColor();
                    rivalValid += 1 + isEye;
                }
            }
        }
    // 计算 bot 能走的格数
    //qDebug() << botValid << rivalValid << 1 - pow(2, -1.0 * pow(sqrt(1.0 * botValid / rivalValid),
                                                               //0.5 + 0.5 * allPoint / pow(CHESSBOARD_SIZE, 2)));
    if(!rivalValid)
        return 1 - pow(2, -1.0 * pow(pow(botValid + 0.1, 0.5),
                                       0.5 + 0.5 * allPoint / pow(CHESSBOARD_SIZE, 2)));
    if(!botValid)
        return 1 - pow(2, -1.0 * pow(pow(rivalValid + 0.1, -0.5),
                                       0.5 + 0.5 * allPoint / pow(CHESSBOARD_SIZE, 2)));
    // 设 botvalid = x, rivalValid = y, allPoint = z, CHESSBOARD_SIZE = k, 估价为 1-2^(-sqrt(x/y)^(0.5+0.5*z/(k*k)))
    return 1 - pow(2, -1.0 * pow(sqrt(1.0 * botValid / rivalValid),
                                   0.5 + 0.5 * allPoint / pow(CHESSBOARD_SIZE, 2)));
}

// minimax 搜索
double Bot::alphaBeta(double a, double b, int depth)
{
    qint64 curTime = QDateTime::currentMSecsSinceEpoch();
    if(curTime - searchStartTime > BOT_TIMEOUT * 900)
        return depth & 1 ? b : a; // 判断超时
    curPlayer = depth & 1;

    int maxDep = 4 + (chooseVec.size() < 45) + (chooseVec.size() < 30) + (chooseVec.size() < 25) + (chooseVec.size() < 23);
    if(maxDep - 1 == depth && curTime - searchStartTime > BOT_TIMEOUT * 300)
        return judgeBoard();
    if(depth >= maxDep)
        return judgeBoard();
    if(depth & 1)
    {
        int op = 0;
        for(auto [xx, yy] : chooseVec)
            if(checkBoard(xx, yy))
            {
                op = 1;
                curBoard[xx][yy] = CurColor();
                b = std::min(b, alphaBeta(a, b, depth + 1));
                curPlayer = depth & 1;
                curBoard[xx][yy] = 0;
                if(a >= b) break;
            }
        if(!op)
            return judgeBoard(); // 叶子节点返回估价
        return b;
    }
    else
    {
        int op = 0;
        for(auto [xx, yy] : chooseVec)
            if(checkBoard(xx, yy))
            {
                op = 1;
                curBoard[xx][yy] = CurColor();
                if(depth == 0) pointChecked ++;
                a = std::max(a, alphaBeta(a, b, depth + 1));
                curPlayer = depth & 1;
                curBoard[xx][yy] = 0;
                if(depth == 0 && a > finalv)
                {
                    finalx = xx, finaly = yy, finalv = a;
                    //qDebug() << xx << "," << yy << " " << a << " " << checkBoard(xx, yy);
                }
                else if(depth == 0)
                   // qDebug() << xx << "," << yy << " " << a << " " << finalv;
                if(a >= b) break;
            }
            //else if(depth == 0) qDebug() << xx << "," << yy <<"???";
        if(!op) return judgeBoard();
        return a;
    }
}
void Bot::run()
{
    searchStartTime = QDateTime::currentMSecsSinceEpoch();

    readFromJudge();
    ItemVector().swap(chooseVec);
    pointChecked = 0;

    if(CHESSBOARD_SIZE != 9) return makeRandomMove();

    for(int x = 0; x < CHESSBOARD_SIZE; x++)
        for(int y = 0; y < CHESSBOARD_SIZE; y++)
        {
            curBoard[x][y] = judge->GridPoint(x, y);
            if(!curBoard[x][y])
            {
                chooseVec.push_back(Point(x, y));
                //qDebug() << x <<"," <<y<<" choosed";
            }
        }

    srand(time(0));
    std::random_shuffle(chooseVec.begin(), chooseVec.end());

    finalx = finaly = finalv = -1;
    alphaBeta(alpha, beta, 0);
    curPlayer = 0;
   // qDebug() << finalx <<"," <<finaly;
    checkBoard(finalx, finaly);
    if(!judge->CheckVaild(finalx, finaly))
    {
        emit timeout();
        return;
    }
    curPlayer = 0;
    judge->PlaceAPiece(finalx, finaly);
    if(judge->runMode == 2 || judge->runMode == 3) // 发送 MOVE_OP 以及处理 recData
        judge->send(NetworkData(OPCODE::MOVE_OP, QString(QChar('A'+finalx))+QString(QChar('1'+finaly)), QString::number(QDateTime::currentMSecsSinceEpoch())));
    double curRatio = judgeBoard();


    if(curRatio < 0.45 && (rand() % 100 + 1) * 0.01 < exp(-eps / 0.03))
    {
        // 对面太强了，退火一下
        alpha -= (0.5 - alpha) * eps;
        beta -= (beta - 0.5) * eps;
    }
    if(curRatio > 0.55 && (rand() % 100 + 1) * 0.01 < exp(-eps / 0.03))
    {
        // 对面太菜了，退火一下
        alpha += (0.5 - alpha) * eps;
        beta += (beta - 0.5) * eps;
    }
    eps *= delta; // 降温

    qDebug() << pointChecked << "<->" << chooseVec.size() << " " << curRatio << " " << alpha << " " << beta << " "
             << QDateTime::currentMSecsSinceEpoch() - searchStartTime;

    ItemVector().swap(chooseVec);
}
void Bot::makeRandomMove()
{
    int x, y;
    do
    {
        qint64 curTime = QDateTime::currentMSecsSinceEpoch();
        if(curTime - searchStartTime > 500)
        {
            emit timeout();
            return;
        }
        x = QRandomGenerator::global()->bounded(CHESSBOARD_SIZE);
        y = QRandomGenerator::global()->bounded(CHESSBOARD_SIZE);
    }
    while(!judge->CheckVaild(x, y));
    judge->PlaceAPiece(x, y);
}
qint64 Bot::getStartTime()
{
    return searchStartTime;
}
