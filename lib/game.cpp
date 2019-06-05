/*
  This file is part of Allie Chess.
  Copyright (C) 2018, 2019 Adam Treat

  Allie Chess is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Allie Chess is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Allie Chess.  If not, see <http://www.gnu.org/licenses/>.

  Additional permission under GNU GPL version 3 section 7
*/

#include "game.h"

#include <QDebug>
#include <QStringList>

#include "bitboard.h"
#include "clock.h"
#include "movegen.h"
#include "node.h"
#include "notation.h"
#include "options.h"
#include "zobrist.h"

using namespace Chess;

static Game s_startPos = Game(QLatin1String("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"));

Game::Game(const QString &fen)
    : m_halfMoveClock(0),
    m_halfMoveNumber(2),
    m_fileOfKingsRook(0),
    m_fileOfQueensRook(0),
    m_repetitions(-1),
    m_hasWhiteKingCastle(false),
    m_hasBlackKingCastle(false),
    m_hasWhiteQueenCastle(false),
    m_hasBlackQueenCastle(false),
    m_activeArmy(Chess::White)
{
    if (fen.isEmpty()) {
        *this = s_startPos;
    } else {
        setFen(fen);
    }
}

bool Game::hasPieceAt(int index, Chess::Army army) const
{
    switch (army) {
    case White:
        return m_whitePositionBoard.testBit(index);
    case Black:
        return m_blackPositionBoard.testBit(index);
    }
    Q_UNREACHABLE();
    return false;
}

PieceType Game::pieceTypeAt(int index) const
{
    // From most numerous piece type to least
    Chess::PieceType type = Pawn;
    if (BitBoard(board(type) & m_whitePositionBoard).testBit(index)
        || BitBoard(board(type) & m_blackPositionBoard).testBit(index))
        return type;

    type = Knight;
    if (BitBoard(board(type) & m_whitePositionBoard).testBit(index)
        || BitBoard(board(type) & m_blackPositionBoard).testBit(index))
        return type;

    type = Bishop;
    if (BitBoard(board(type) & m_whitePositionBoard).testBit(index)
        || BitBoard(board(type) & m_blackPositionBoard).testBit(index))
        return type;

    type = Rook;
    if (BitBoard(board(type) & m_whitePositionBoard).testBit(index)
        || BitBoard(board(type) & m_blackPositionBoard).testBit(index))
        return type;

    type = Queen;
    if (BitBoard(board(type) & m_whitePositionBoard).testBit(index)
        || BitBoard(board(type) & m_blackPositionBoard).testBit(index))
        return type;

    type = King;
    if (BitBoard(board(type) & m_whitePositionBoard).testBit(index)
        || BitBoard(board(type) & m_blackPositionBoard).testBit(index))
        return type;

    return Unknown;
}

bool Game::hasPieceTypeAt(int index, Chess::PieceType piece) const
{
    return board(piece).testBit(index);
}

bool Game::makeMove(const Move &move)
{
    Move mv = move;
    bool ok = fillOutMove(activeArmy(), &mv);
    if (!ok) {
        qDebug() << "ERROR! move is malformed" << Notation::moveToString(move, Chess::Computer);
        return false;
    }
    processMove(activeArmy(), mv);
    return true;
}

void Game::processMove(Chess::Army army, const Move &move)
{
    m_lastMove = move;
    m_enPassantTarget = Square();

    if (army == White) {
        if (move.piece() == King) {
            m_hasWhiteKingCastle = false;
            m_hasWhiteQueenCastle = false;
        } else if (move.piece() == Rook) {
            if (move.start() == Square(m_fileOfQueensRook, 0))
                m_hasWhiteQueenCastle = false;
            else if (move.start() == Square(m_fileOfKingsRook, 0))
                m_hasWhiteKingCastle = false;
        } else if (move.piece() == Pawn && qAbs(move.start().rank() - move.end().rank()) == 2) {
            m_enPassantTarget = Square(move.end().file(), move.end().rank() - 1);
        }

        int start = move.start().data();
        int end = move.end().data();

        bool capture = hasPieceAt(end, Black) || move.isEnPassant();
        if (capture) {
            m_lastMove.setCapture(true); // set the flag now that we know it
            int capturedPieceIndex = end;
            if (move.isEnPassant())
                capturedPieceIndex = Square(move.end().file(), move.end().rank() - 1).data();

            PieceType type = pieceTypeAt(capturedPieceIndex);
            Q_ASSERT(type != Unknown);
            togglePieceAt(capturedPieceIndex, Black, type, false);
            if (type == Rook) {
                if (move.end().file() == m_fileOfKingsRook)
                    m_hasBlackKingCastle = false;
                else
                    m_hasBlackQueenCastle = false;
            }
        }

        if (move.piece() != Pawn && !capture)
            ++m_halfMoveClock;
        else
            m_halfMoveClock = 0;

        togglePieceAt(start, White, move.piece(), false);

        if (move.isCastle()) { //have to move the rook
            if (move.castleSide() == KingSide) {
                Square rook(m_fileOfKingsRook, 0);
                togglePieceAt(BitBoard::squareToIndex(rook), White, Rook, false);
                rook = Square(5, 0); //f1
                togglePieceAt(BitBoard::squareToIndex(rook), White, Rook, true);
                Square king(6, 0); //g1
                togglePieceAt(BitBoard::squareToIndex(king), White, King, true);
            } else if (move.castleSide() == QueenSide) {
                Square rook(m_fileOfQueensRook, 0);
                togglePieceAt(BitBoard::squareToIndex(rook), White, Rook, false);
                rook = Square(3, 0); //d1
                togglePieceAt(BitBoard::squareToIndex(rook), White, Rook, true);
                Square king(2, 0); //c1
                togglePieceAt(BitBoard::squareToIndex(king), White, King, true);
            }
        } else if (move.promotion() != Unknown) {
            togglePieceAt(end, White, move.promotion(), true);
        } else if (!move.isCastle()) {
            togglePieceAt(end, White, move.piece(), true);
        }
    } else if (army == Black) {
        if (move.piece() == King) {
            m_hasBlackKingCastle = false;
            m_hasBlackQueenCastle = false;
        } else if (move.piece() == Rook) {
            if (move.start() == Square(m_fileOfQueensRook, 7))
                m_hasBlackQueenCastle = false;
            else if (move.start() == Square(m_fileOfKingsRook, 7))
                m_hasBlackKingCastle = false;
        } else if (move.piece() == Pawn && qAbs(move.start().rank() - move.end().rank()) == 2) {
            m_enPassantTarget = Square(move.end().file(), move.end().rank() + 1);
        }

        int start = move.start().data();
        int end = move.end().data();

        bool capture = hasPieceAt(end, White) || move.isEnPassant();
        if (capture) {
            m_lastMove.setCapture(true); // set the flag now that we know it
            int capturedPieceIndex = end;
            if (move.isEnPassant())
                capturedPieceIndex = Square(move.end().file(), move.end().rank() + 1).data();

            PieceType type = pieceTypeAt(capturedPieceIndex);
            Q_ASSERT(type != Unknown);
            togglePieceAt(capturedPieceIndex, White, type, false);
            if (type == Rook) {
                if (move.end().file() == m_fileOfKingsRook)
                    m_hasWhiteKingCastle = false;
                else
                    m_hasWhiteQueenCastle = false;
            }
        }

        if (move.piece() != Pawn && !capture)
            ++m_halfMoveClock;
        else
            m_halfMoveClock = 0;

        togglePieceAt(start, Black, move.piece(), false);

        if (move.isCastle()) { //have to move the rook
            if (move.castleSide() == KingSide) {
                Square rook(m_fileOfKingsRook, 7);
                togglePieceAt(BitBoard::squareToIndex(rook), Black, Rook, false);
                rook = Square(5, 7); //f8
                togglePieceAt(BitBoard::squareToIndex(rook), Black, Rook, true);
                Square king(6, 7); //g8
                togglePieceAt(BitBoard::squareToIndex(king), Black, King, true);
            } else if (move.castleSide() == QueenSide) {
                Square rook(m_fileOfQueensRook, 7);
                togglePieceAt(BitBoard::squareToIndex(rook), Black, Rook, false);
                rook = Square(3, 7); //d8
                togglePieceAt(BitBoard::squareToIndex(rook), Black, Rook, true);
                Square king(2, 7); //c8
                togglePieceAt(BitBoard::squareToIndex(king), Black, King, true);
            }
        } else if (move.promotion() != Unknown) {
            togglePieceAt(end, Black, move.promotion(), true);
        } else {
            togglePieceAt(end, Black, move.piece(), true);
        }
    }

    m_repetitions = -1;
    m_halfMoveNumber++;
    m_activeArmy = m_activeArmy == White ? Black : White;
}

bool Game::fillOutMove(Chess::Army army, Move *move) const
{
    if (move->isCastle() && !move->isValid()) {
        if (move->castleSide() == KingSide)
            move->setEnd(Square(6, army == White ? 0 : 7));
        else if (move->castleSide() == QueenSide)
            move->setEnd(Square(2, army == White ? 0 : 7));
    }

    if (!move->isValid()) {
        qDebug() << "invalid move...";
        return false; //not enough info to do anything
    }

    if (move->piece() == Unknown) {
        int start = move->start().data();
        move->setPiece(pieceTypeAt(start));
    }

    if (move->piece() == Unknown)
        return false;

    if (!move->start().isValid()) {
        bool ok = fillOutStart(army, move);
        if (!ok)
            return false;
    }

    if (move->piece() == Pawn && move->promotion() == Unknown &&
        ((army == White && move->end().rank() == 7) ||
         (army == Black && move->end().rank() == 0))) {
        QString promotion = "Queen"; // FIXME
        if (promotion == "Queen")
            move->setPromotion(Queen);
        else if (promotion == "Rook")
            move->setPromotion(Rook);
        else if (promotion == "Bishop")
            move->setPromotion(Bishop);
        else if (promotion == "Knight")
            move->setPromotion(Knight);
        else
            move->setPromotion(Queen);
    }

    if (move->piece() == Pawn && move->end() == m_enPassantTarget)
        move->setEnPassant(true);

    if (move->piece() == King && !move->isCastle()) {
        const int rankStart = move->start().rank();
        const int rankEnd = move->end().rank();
        if ((rankStart == 0 && rankEnd == 0) || (rankStart == 7 && rankEnd == 7)) {
            const int fileStart = move->start().file();
            const int fileEnd = move->end().file();
            if (fileStart == 4 && fileEnd == 6) {
                move->setCastleSide(KingSide);
                move->setCastle(true);
            } else if (fileStart == 4 && fileEnd == 2) {
                move->setCastle(true);
                move->setCastleSide(QueenSide);
            } else if (Options::globalInstance()->option("UCI_Chess960").value() == QLatin1String("true")
                && !BitBoard(board(army) & board(Rook) & move->end()).isClear()) {
                if (fileEnd == fileOfKingsRook()) {
                    move->setCastleSide(KingSide);
                    move->setCastle(true);
                } else if (fileEnd == fileOfQueensRook()) {
                    move->setCastleSide(QueenSide);
                    move->setCastle(true);
                } else {
                    Q_UNREACHABLE();
                }
            }
        }
    }

    return true;
}

bool Game::fillOutStart(Chess::Army army, Move *move) const
{
    if (!move->isValid()) {
        qDebug() << "invalid move...";
        return false; //not enough info to do anything
    }

    Square square = move->start();
    if (square.isValid())
        return true;

    BitBoard positions(board(move->piece()) & board(army));
    BitBoard opposingPositions(board(army == White ? Black : White));

    qDebug() << "could not find a valid start square..."
             << "\narmy:" << (army == White ? "White" : "Black")
             << "\nmove:" << Notation::moveToString(*move)
             << "\npositions:" << positions
             << "\nopposingPositions:" << opposingPositions;

    return false;
}

QPair<Chess::Castle, Square> castlingFromFen(const QChar c, const Square &king,
    const QVector<Square> &rooks)
{
    const bool isChess960 = Options::globalInstance()->option("UCI_Chess960").value() == QLatin1String("true");
    QPair<Chess::Castle, Square> result;

    // Support ill-formed or fabricated fen
    if (rooks.isEmpty() || !king.isValid()) {
        result.first = (c == QChar('k') ? KingSide : QueenSide);
        return result;
    }

    if (c == QChar('k')) {
        result.first = KingSide;
        result.second = rooks.last();
        Q_ASSERT(result.second.file() > king.file());
    } else if (c == QChar('q')) {
        result.first = QueenSide;
        result.second = rooks.first();
        Q_ASSERT(result.second.file() < king.file());
    } else {
        Q_ASSERT(isChess960);
        for (Square sq : rooks) {
            if (Notation::fileToChar(sq.file()) == c) {
                result.second = sq;
                result.first = sq.file() > king.file() ? KingSide : QueenSide;
                break;
            }
        }
    }

    Q_ASSERT(result.second.isValid());
    Q_ASSERT(result.second.file() != king.file());
    return result;
}

QChar fenFromCastling(Chess::Castle castle, const Square &king, const QVector<Square> &rooks,
    int fileOfCastlingRook)
{
    // Support ill-formed or fabricated fen
    if (rooks.isEmpty()) {
        if (castle == KingSide)
            return QChar('k');
        else
            return QChar('q');
    }

    QVector<Square> rooksToTheLeft;
    QVector<Square> rooksToTheRight;
    for (Square sq : rooks) {
        if (sq.file() < king.file())
            rooksToTheLeft.append(sq);
        else if (sq.file() > king.file())
            rooksToTheRight.append(sq);
        else
            Q_UNREACHABLE();
    }

    const bool isChess960 = Options::globalInstance()->option("UCI_Chess960").value() == QLatin1String("true");
    if (castle == KingSide) {
        Q_ASSERT(!rooksToTheRight.isEmpty());
        if (rooksToTheRight.last().file() == fileOfCastlingRook)
            return QChar('k');
        else {
            Q_ASSERT(isChess960);
            for (Square sq : rooksToTheRight) {
                if (sq.file() == fileOfCastlingRook)
                    return Notation::fileToChar(fileOfCastlingRook);
            }
            Q_UNREACHABLE();
        }
    } else {
        Q_ASSERT(!rooksToTheLeft.isEmpty());
        if (rooksToTheLeft.first().file() == fileOfCastlingRook)
            return QChar('q');
        else {
            Q_ASSERT(isChess960);
            for (Square sq : rooksToTheLeft) {
                if (sq.file() == fileOfCastlingRook)
                    return Notation::fileToChar(fileOfCastlingRook);
            }
            Q_UNREACHABLE();
        }
    }
    Q_UNREACHABLE();
}

void Game::setFen(const QString &fen)
{
    m_activeArmy = White;

    m_halfMoveClock = 0;
    m_halfMoveNumber = 2; // fen assumes fullmovenumber starts with 1
    m_fileOfKingsRook = 0;
    m_fileOfQueensRook = 0;

    m_enPassantTarget = Square();

    m_whitePositionBoard = BitBoard();
    m_blackPositionBoard = BitBoard();
    m_kingsBoard = BitBoard();
    m_queensBoard = BitBoard();
    m_rooksBoard = BitBoard();
    m_bishopsBoard = BitBoard();
    m_knightsBoard = BitBoard();
    m_pawnsBoard = BitBoard();
    m_hasWhiteKingCastle = false;
    m_hasBlackKingCastle = false;
    m_hasWhiteQueenCastle = false;
    m_hasBlackQueenCastle = false;

    QStringList list = fen.split(' ');
    Q_ASSERT(list.count() >= 4);

    QStringList ranks = list.at(0).split('/');
    Q_ASSERT(ranks.count() == 8);

    QVector<Square> whiteRooks;
    QVector<Square> blackRooks;
    Square whiteKing;
    Square blackKing;

    for (int i = 0; i < ranks.size(); ++i) {
        QString rank = ranks.at(i);
        int blank = 0;
        for (int j = 0; j < rank.size(); ++j) {
            QChar c = rank.at(j);
            const int f = j + blank;
            const int r = 7 - i;
            const Square square = Square(f, r);
            if (c.isLetter() && c.isUpper()) /*white*/ {
                PieceType piece = Notation::charToPiece(c);
                togglePieceAt(square.data(), White, piece, true);
                if (piece == PieceType::Rook) {
                    whiteRooks.append(square);
                } else if (piece == PieceType::King)
                    whiteKing = square;
            } else if (c.isLetter() && c.isLower()) /*black*/ {
                PieceType piece = Notation::charToPiece(c.toUpper());
                togglePieceAt(square.data(), Black, piece, true);
                if (piece == PieceType::Rook) {
                    blackRooks.append(square);
                } else if (piece == PieceType::King)
                    blackKing = square;
            } else if (c.isNumber()) /*blank*/ {
                blank += QString(c).toInt() - 1;
            }
        }
    }

    Q_ASSERT(whiteKing.isValid());
    Q_ASSERT(blackKing.isValid());

    // Sort the rooks by file
    std::stable_sort(whiteRooks.begin(), whiteRooks.end(),
        [=](const Square &a, const Square &b) {
        return a.file() < b.file();
    });

    std::stable_sort(blackRooks.begin(), blackRooks.end(),
        [=](const Square &a, const Square &b) {
        return a.file() < b.file();
    });

    m_activeArmy = list.at(1) == QLatin1String("w") ? White : Black;

    //Should work for regular fen and UCI fen for chess960...
    QString castling = list.at(2);
    if (castling != "-") {
        for (QChar c : castling) {
            const Chess::Army castleArmy = c.isUpper() ? White : Black;
            QPair<Chess::Castle, Square> pair;
            if (castleArmy == White) {
                pair = castlingFromFen(c.toLower(), whiteKing, whiteRooks);
                if (pair.first == KingSide) {
                    m_hasWhiteKingCastle = true;
                    m_fileOfKingsRook = quint8(pair.second.file());
                } else {
                    m_hasWhiteQueenCastle = true;
                    m_fileOfQueensRook = quint8(pair.second.file());
                }
            } else {
                pair = castlingFromFen(c.toLower(), blackKing, blackRooks);
                if (pair.first == KingSide) {
                    m_hasBlackKingCastle = true;
                    m_fileOfKingsRook = quint8(pair.second.file());
                } else {
                    m_hasBlackQueenCastle = true;
                    m_fileOfQueensRook = quint8(pair.second.file());
                }
            }
        }
    }

    QString enPassant = list.at(3);
    if (enPassant != QLatin1String("-"))
        m_enPassantTarget = Notation::stringToSquare(enPassant);

    if (list.count() > 4)
        m_halfMoveClock = quint16(list.at(4).toInt());
    if (list.count() > 5)
        m_halfMoveNumber = quint16(qCeil(list.at(5).toInt() * 2.0));
}

QString Game::stateOfGameToFen(bool includeMoveNumbers) const
{
    QVector<Square> whiteRooks;
    QVector<Square> blackRooks;
    Square whiteKing;
    Square blackKing;

    QStringList rankList;
    for (qint8 i = 0; i < 8; ++i) { //rank
        QString rank;
        int blank = 0;
        for (qint8 j = 0; j < 8; ++j) { //file
            Square square(j, 7 - i);
            int index = square.data();
            if (hasPieceAt(index, White)) {
                PieceType piece = pieceTypeAt(index);
                QChar ch = Notation::pieceToChar(piece);
                if (blank > 0) {
                    rank += QString::number(blank);
                    blank = 0;
                }
                if (!ch.isNull()) {
                    rank += ch.toUpper();
                } else {
                    rank += 'P';
                }
                if (piece == PieceType::Rook)
                    whiteRooks.append(square);
                else if (piece == PieceType::King)
                    whiteKing = square;
            } else if (hasPieceAt(index, Black)) {
                PieceType piece = pieceTypeAt(index);
                QChar ch = Notation::pieceToChar(piece);
                if (blank > 0) {
                    rank += QString::number(blank);
                    blank = 0;
                }
                if (!ch.isNull()) {
                    rank += ch.toLower();
                } else {
                    rank += 'p';
                }
                if (piece == PieceType::Rook)
                    blackRooks.append(square);
                else if (piece == PieceType::King)
                    blackKing = square;
            } else {
                blank++;
            }
        }

        if (blank > 0) {
            rank += QString::number(blank);
            blank = 0;
        }

        rankList << rank;
    }

    Q_ASSERT(whiteKing.isValid());
    Q_ASSERT(blackKing.isValid());

    // Sort the rooks by file
    std::stable_sort(whiteRooks.begin(), whiteRooks.end(),
        [=](const Square &a, const Square &b) {
        return a.file() < b.file();
    });

    std::stable_sort(blackRooks.begin(), blackRooks.end(),
        [=](const Square &a, const Square &b) {
        return a.file() < b.file();
    });

    QString ranks = rankList.join("/");
    QString activeArmy = (m_activeArmy == White ? QLatin1String("w") : QLatin1String("b"));

    QString castling;
    if (isCastleAvailable(White, KingSide))
        castling.append(fenFromCastling(KingSide, whiteKing, whiteRooks, m_fileOfKingsRook).toUpper());
    if (isCastleAvailable(White, QueenSide))
        castling.append(fenFromCastling(QueenSide, whiteKing, whiteRooks, m_fileOfQueensRook).toUpper());
    if (isCastleAvailable(Black, KingSide))
        castling.append(fenFromCastling(KingSide, blackKing, blackRooks, m_fileOfKingsRook));
    if (isCastleAvailable(Black, QueenSide))
        castling.append(fenFromCastling(QueenSide, blackKing, blackRooks, m_fileOfQueensRook));
    if (castling.isEmpty())
        castling.append("-");

    QString enPassant = enPassantTarget().isValid() ? Notation::squareToString(enPassantTarget()) : QLatin1String("-");

    QStringList fen;
    fen << ranks << activeArmy << castling << enPassant;
    if (includeMoveNumbers)
        fen << QString::number(halfMoveClock()) << QString::number(qCeil(halfMoveNumber() / 2.0));

    return fen.join(" ");
}

BitBoard Game::kingAttackBoard(Chess::Army army, const Movegen *gen) const
{
    BitBoard bits;
    const BitBoard friends = army == White ? m_whitePositionBoard : m_blackPositionBoard;
    const BitBoard enemies = army == Black ? m_whitePositionBoard : m_blackPositionBoard;
    const BitBoard pieces(friends & board(King));
    BitBoard::Iterator sq = pieces.begin();
    for (int i = 0; sq != pieces.end(); ++sq, ++i) {
        Q_ASSERT(i < 1);
        bits = bits | gen->kingMoves(*sq, friends, enemies);
    }
    return bits;
}

BitBoard Game::queenAttackBoard(Chess::Army army, const Movegen *gen) const
{
    BitBoard bits;
    const BitBoard friends = army == White ? m_whitePositionBoard : m_blackPositionBoard;
    const BitBoard enemies = army == Black ? m_whitePositionBoard : m_blackPositionBoard;
    const BitBoard pieces(friends & board(Queen));
    BitBoard::Iterator sq = pieces.begin();
    for (; sq != pieces.end(); ++sq)
        bits = bits | gen->queenMoves(*sq, friends, enemies);
    return bits;
}

BitBoard Game::rookAttackBoard(Chess::Army army, const Movegen *gen) const
{
    BitBoard bits;
    const BitBoard friends = army == White ? m_whitePositionBoard : m_blackPositionBoard;
    const BitBoard enemies = army == Black ? m_whitePositionBoard : m_blackPositionBoard;
    const BitBoard pieces(friends & board(Rook));
    BitBoard::Iterator sq = pieces.begin();
    for (; sq != pieces.end(); ++sq)
        bits = bits | gen->rookMoves(*sq, friends, enemies);
    return bits;
}

BitBoard Game::bishopAttackBoard(Chess::Army army, const Movegen *gen) const
{
    BitBoard bits;
    const BitBoard friends = army == White ? m_whitePositionBoard : m_blackPositionBoard;
    const BitBoard enemies = army == Black ? m_whitePositionBoard : m_blackPositionBoard;
            const BitBoard pieces(friends & board(Bishop));
            BitBoard::Iterator sq = pieces.begin();
            for (; sq != pieces.end(); ++sq)
                bits = bits | gen->bishopMoves(*sq, friends, enemies);
            return bits;
}

BitBoard Game::knightAttackBoard(Chess::Army army, const Movegen *gen) const
{
    BitBoard bits;
    const BitBoard friends = army == White ? m_whitePositionBoard : m_blackPositionBoard;
    const BitBoard enemies = army == Black ? m_whitePositionBoard : m_blackPositionBoard;
    const BitBoard pieces(friends & board(Knight));
    BitBoard::Iterator sq = pieces.begin();
    for (; sq != pieces.end(); ++sq)
        bits = bits | gen->knightMoves(*sq, friends, enemies);
    return bits;
}

BitBoard Game::pawnAttackBoard(Chess::Army army, const Movegen *gen) const
{
    BitBoard bits;
    const BitBoard friends = army == White ? m_whitePositionBoard : m_blackPositionBoard;
    const BitBoard enemies = army == Black ? m_whitePositionBoard : m_blackPositionBoard;
    const BitBoard pieces(friends & board(Pawn));
    BitBoard::Iterator sq = pieces.begin();
    BitBoard enemiesPlusEnpassant = enemies;
    if (m_enPassantTarget.isValid())
        enemiesPlusEnpassant.setSquare(m_enPassantTarget);
    for (; sq != pieces.end(); ++sq)
        bits = bits | gen->pawnAttacks(army, *sq, friends, enemiesPlusEnpassant);
    return bits;
}

void Game::pseudoLegalMoves(Node *parent) const
{
    const Chess::Army army = activeArmy();
    const BitBoard friends = army == White ? m_whitePositionBoard : m_blackPositionBoard;
    const BitBoard enemies = army == Black ? m_whitePositionBoard : m_blackPositionBoard;
    const Movegen *gen = Movegen::globalInstance();

    {
        const BitBoard pieces(friends & board(King));
        BitBoard::Iterator sq = pieces.begin();
        for (int i = 0; sq != pieces.end(); ++sq, ++i) {
            Q_ASSERT(i < 1);
            const BitBoard moves = gen->kingMoves(*sq, friends, enemies);
            BitBoard::Iterator newSq = moves.begin();
            for (; newSq != moves.end(); ++newSq)
                generateMove(King, *sq, *newSq, parent);
        }
    }

    {
        const BitBoard pieces(friends & board(Queen));
        BitBoard::Iterator sq = pieces.begin();
        for (; sq != pieces.end(); ++sq) {
            const BitBoard moves = gen->queenMoves(*sq, friends, enemies);
            BitBoard::Iterator newSq = moves.begin();
            for (; newSq != moves.end(); ++newSq)
                generateMove(Queen, *sq, *newSq, parent);
        }
    }

    {
        const BitBoard pieces(friends & board(Rook));
        BitBoard::Iterator sq = pieces.begin();
        for (; sq != pieces.end(); ++sq) {
            const BitBoard moves = gen->rookMoves(*sq, friends, enemies);
            BitBoard::Iterator newSq = moves.begin();
            for (; newSq != moves.end(); ++newSq)
                generateMove(Rook, *sq, *newSq, parent);
        }
    }

    {
        const BitBoard pieces(friends & board(Bishop));
        BitBoard::Iterator sq = pieces.begin();
        for (; sq != pieces.end(); ++sq) {
            const BitBoard moves = gen->bishopMoves(*sq, friends, enemies);
            BitBoard::Iterator newSq = moves.begin();
            for (; newSq != moves.end(); ++newSq)
                generateMove(Bishop, *sq, *newSq, parent);
        }
    }

    {
        const BitBoard pieces(friends & board(Knight));
        BitBoard::Iterator sq = pieces.begin();
        for (; sq != pieces.end(); ++sq) {
            const BitBoard moves = gen->knightMoves(*sq, friends, enemies);
            BitBoard::Iterator newSq = moves.begin();
            for (; newSq != moves.end(); ++newSq)
                generateMove(Knight, *sq, *newSq, parent);
        }
    }

    {
        const BitBoard pieces(friends & board(Pawn));
        BitBoard::Iterator sq = pieces.begin();
        BitBoard enemiesPlusEnpassant = enemies;
        if (m_enPassantTarget.isValid())
            enemiesPlusEnpassant.setSquare(m_enPassantTarget);
        for (; sq != pieces.end(); ++sq) {
            {
                const BitBoard moves = gen->pawnMoves(army, *sq, friends, enemies);
                BitBoard::Iterator newSq = moves.begin();
                for (; newSq != moves.end(); ++newSq) {
                    bool forwardTwo = qAbs((*newSq).rank() - (*sq).rank()) > 1;
                    Square forwardOne = Square((*newSq).file(), army == White ? (*newSq).rank() - 1 : (*newSq).rank() + 1);
                    if (forwardTwo && BitBoard(friends | enemies).testBit(forwardOne.data()))
                        continue; // can't move through another piece
                    generateMove(Pawn, *sq, *newSq, parent);
                }
            }
            {
                const BitBoard moves = gen->pawnAttacks(army, *sq, friends, enemiesPlusEnpassant);
                BitBoard::Iterator newSq = moves.begin();
                for (; newSq != moves.end(); ++newSq)
                    generateMove(Pawn, *sq, *newSq, parent);
            }
        }
    }

    // Add castle moves
    if (isCastleLegal(army, KingSide))
        generateCastle(army, KingSide, parent);
    if (isCastleLegal(army, QueenSide))
        generateCastle(army, QueenSide, parent);
}

void Game::generateCastle(Chess::Army army, Chess::Castle castleSide, Node *parent) const
{
    Move mv;
    mv.setPiece(King);
    mv.setStart(BitBoard(board(King) & board(army)).occupiedSquares().first());

    // All castles are encoded internally as king takes chosen castling rook
    if (castleSide == KingSide)
        mv.setEnd(army == White ? Square(fileOfKingsRook(), 0) : Square(fileOfKingsRook(), 7));
    else
        mv.setEnd(army == White ? Square(fileOfQueensRook(), 0) : Square(fileOfQueensRook(), 7));

    mv.setCastle(true);
    mv.setCastleSide(castleSide);
    Q_ASSERT(parent);
    parent->generatePotential(mv);
}

void Game::generateMove(Chess::PieceType piece, const Square &start, const Square &end, Node *parent) const
{
    const Chess::Army army = activeArmy();
    const bool isPromotion = piece == Pawn && (army == White ? end.rank() == 7 : end.rank() == 0);
    const bool isCapture = board(army == White ? Black : White).isSquareOccupied(end);

    Move mv;
    mv.setPiece(piece);
    mv.setStart(start);
    mv.setEnd(end);
    mv.setCapture(isCapture);
    Q_ASSERT(parent);
    if (!isPromotion) {
        parent->generatePotential(mv);
    } else {
        mv.setPromotion(Queen);
        parent->generatePotential(mv);
        mv.setPromotion(Knight);
        parent->generatePotential(mv);
        mv.setPromotion(Rook);
        parent->generatePotential(mv);
        mv.setPromotion(Bishop);
        parent->generatePotential(mv);
    }
}

bool Game::isChecked(Chess::Army army)
{
    const Chess::Army friends = army == White ? White : Black;
    const Chess::Army enemies = army == Black ? White : Black;
    const BitBoard kingBoard(board(friends) & board(King));
    const Movegen *gen = Movegen::globalInstance();
    {
        const BitBoard b(kingBoard & queenAttackBoard(enemies, gen));
        if (!b.isClear()) {
            m_lastMove.setCheck(true);
            return true;
        }
    }
    {
        const BitBoard b(kingBoard & rookAttackBoard(enemies, gen));
        if (!b.isClear()) {
            m_lastMove.setCheck(true);
            return true;
        }
    }
    {
        const BitBoard b(kingBoard & bishopAttackBoard(enemies, gen));
        if (!b.isClear()) {
            m_lastMove.setCheck(true);
            return true;
        }
    }
    {
        const BitBoard b(kingBoard & knightAttackBoard(enemies, gen));
        if (!b.isClear()) {
            m_lastMove.setCheck(true);
            return true;
        }
    }
    {
        // Checks for illegality...
        const BitBoard b(kingBoard & kingAttackBoard(enemies, gen));
        if (!b.isClear()) {
            m_lastMove.setCheck(true);
            return true;
        }
    }
    {
        const BitBoard b(kingBoard & pawnAttackBoard(enemies, gen));
        if (!b.isClear()) {
            m_lastMove.setCheck(true);
            return true;
        }
    }
    m_lastMove.setCheck(false);
    return false;
}

void Game::setCheckMate(bool checkMate)
{
    m_lastMove.setCheckMate(checkMate);
}

void Game::setStaleMate(bool staleMate)
{
    m_lastMove.setStaleMate(staleMate);
}

BitBoard boardBetweenOnSameRank(const Square &a, const Square &b, bool inclusive)
{
    Q_ASSERT(a.rank() == b.rank());
    BitBoard result;
    if (inclusive) {
        result.setBit(a.data(), true);
        result.setBit(b.data(), true);
    }

    const int files = qAbs(a.file() - b.file()) - 1;
    const int start = (a.file() < b.file() ? a.file() : b.file()) + 1;
    for (int f = start; f < start + files; ++f)
        result.setBit(Square(f, a.rank()).data(), true);

    return result;
}

bool Game::isCastleLegal(Chess::Army army, Chess::Castle castle) const
{
    // 1) The king and the chosen rook are on the player's first rank.
    // 2) Neither the king nor the chosen rook has previously moved.
    if (!isCastleAvailable(army, castle))
        return false;

    // Get the chosen rook
    const BitBoard rookBoard(Square(castle == KingSide ? fileOfKingsRook() : fileOfQueensRook(), army == White ? 0 : 7)
        & board(Rook) & board(army));

    // If it does not exist, then castle is clearly illegal
    if (rookBoard.isClear())
        return false;

    Q_ASSERT(rookBoard.occupiedSquares().count() == 1);
    const Square chosenRook = rookBoard.occupiedSquares().first();

    // Get the king
    const BitBoard kingBoard(board(King) & board(army));
    Q_ASSERT(rookBoard.occupiedSquares().count() == 1);
    const Square king = kingBoard.occupiedSquares().first();

    // Get the board between king and chosen rook
    const BitBoard pieces = BitBoard(board(White) | board(Black));
    const BitBoard between = boardBetweenOnSameRank(king, chosenRook, false /*inclusive*/)
        & pieces;

    // 3) There are no pieces between the king and the chosen rook.
    if (!between.isClear())
        return false;

    const Square kingFrom = king;
    const Square rookFrom = chosenRook;

    const Square kingTo = Square((castle == KingSide ? 6 : 2), king.rank());
    const BitBoard kingMovesThrough = boardBetweenOnSameRank(kingFrom, kingTo, true /*inclusive*/);

    const Square rookTo = Square((castle == KingSide ? 5 : 3), chosenRook.rank());
    const BitBoard rookMovesThrough = boardBetweenOnSameRank(rookFrom, rookTo, true /*inclusive*/);

    // Rook and King cannot jump over anything but each other
    const BitBoard throughMinusKAndR(BitBoard(BitBoard(kingMovesThrough | rookMovesThrough)
        ^ rookBoard ^ kingBoard) & pieces);
    if (!throughMinusKAndR.isClear())
        return false;

    const Movegen *gen = Movegen::globalInstance();
    const Chess::Army attackArmy = army == White ? Black : White;
    const BitBoard atb = kingAttackBoard(attackArmy, gen) |
        queenAttackBoard(attackArmy, gen) |
        rookAttackBoard(attackArmy, gen) |
        bishopAttackBoard(attackArmy, gen) |
        knightAttackBoard(attackArmy, gen) |
        pawnAttackBoard(attackArmy, gen);

    // 4) The king is not currently in check.
    // 5) The king does not pass through a square that is attacked by an enemy piece.
    // 6) The king does not end up in check. (True of any legal move.)
    if (!BitBoard(kingMovesThrough & atb).isClear())
        return false;

    return true;
}

bool Game::isSamePosition(const Game &other) const
{
    // FIXME: For purposes of 3-fold it does not matter if the queens rook and kings rook have
    // swapped places, but it does matter for purposes of hash
    return m_activeArmy == other.m_activeArmy
        && m_fileOfKingsRook == other.m_fileOfKingsRook
        && m_fileOfQueensRook == other.m_fileOfQueensRook
        && m_enPassantTarget == other.m_enPassantTarget
        && m_whitePositionBoard == other.m_whitePositionBoard
        && m_blackPositionBoard == other.m_blackPositionBoard
        && m_kingsBoard == other.m_kingsBoard
        && m_queensBoard == other.m_queensBoard
        && m_rooksBoard == other.m_rooksBoard
        && m_bishopsBoard == other.m_bishopsBoard
        && m_knightsBoard == other.m_knightsBoard
        && m_pawnsBoard == other.m_pawnsBoard
        && m_hasWhiteKingCastle == other.m_hasWhiteKingCastle
        && m_hasBlackKingCastle == other.m_hasBlackKingCastle
        && m_hasWhiteQueenCastle == other.m_hasWhiteQueenCastle
        && m_hasBlackQueenCastle ==  other.m_hasBlackQueenCastle;
}

quint64 Game::hash() const
{
    return Zobrist::globalInstance()->hash(*this);
}

int Game::materialScore(Chess::Army army) const
{
    int score = 0;

    {
        BitBoard pieces(board(army) & board(Queen));
        score += pieces.count() * 9;
    }

    {
        BitBoard pieces(board(army) & board(Rook));
        score += pieces.count() * 5;
    }

    {
        BitBoard pieces(board(army) & board(Bishop));
        score += pieces.count() * 3;
    }

    {
        BitBoard pieces(board(army) & board(Knight));
        score += pieces.count() * 3;
    }

    {
        BitBoard pieces(board(army) & board(Pawn));
        score += pieces.count() * 1;
    }

    return score;
}

bool Game::isDeadPosition() const
{
    // If queens, rooks, or pawns are on the board, then we are good
    if (!board(Queen).isClear())
        return false;

    if (!board(Rook).isClear())
        return false;

    if (!board(Pawn).isClear())
        return false;

    // If game has four or more pieces, then usually someone can still mate although it might not be
    // forcing or if bishops are opposite then it is dead (FIXME)
    if (BitBoard(board(White) | board(Black)).count() > 3)
        return false;

    // If only 3 pieces remain with none of the above, then no one can mate
    // ie, it has to be either KBvK, or KNvK, KvK
    return true;
}

QString Game::toString(NotationType type) const
{
    if (lastMove().isValid())
        return Notation::moveToString(lastMove(), type);
    else
        return "start";
}

QDebug operator<<(QDebug debug, const Game &g)
{
    debug << g.toString(Chess::Standard);
    return debug.nospace();
}
