//
//  MeshFactory.cpp
//
//
//
//                      2009.09.29
//			2008.11.05
//			k.Takeda
#include "ElementType.h"
#include "BoundaryMesh.h"
#include "BoundaryParts.h"
#include "BoundaryFace.h"
#include "BndVertex.h"
#include "BoundaryNodeMesh.h"
#include "BoundaryFaceMesh.h"
#include "CommNode.h"
#include "CommMesh2.h"
#include "BoundaryNode.h"


#include <vector>

#include "FaceTree.h"
#include "EdgeTree.h"
#include "ContactNode.h"
#include "GMGModel.h"
#include "CommFace.h"
#include "ContactMesh.h"
#include "AssyModel.h"


#include "Element.h"


#include "Mesh.h"
#include "MeshFactory.h"
#include "BoundaryHexa.h"
#include "SkinFace.h"
using namespace pmw;

// construct & destruct
//
CMeshFactory::CMeshFactory(void)
{
    mpLogger = Utility::CLogger::Instance();
}

CMeshFactory::~CMeshFactory(void)
{
    //cout << "~CMeshFactory" << endl;
}

// MGを構築しない場合は、MeshのsetupAggregateだけを構築
//
void CMeshFactory::refineMesh()
{
    if(mMGLevel > 0){
        MGMeshConstruct();// Multi Gridを構築
    }else{
        SGMeshConstruct();// Single Gridを構築
    }
}

// Single Grid
//
void CMeshFactory::SGMeshConstruct()
{
    CAssyModel *pAssy;
    CMesh      *pMesh;

    pAssy = mpGMGModel->getAssyModel(0);

    uiint nLevel=0;
    uiint numOfMesh = pAssy->getNumOfMesh();
    uiint imesh;
    for(imesh=0; imesh < numOfMesh; imesh++){
        pMesh = pAssy->getMesh(imesh);

        pMesh->setupAggregate(nLevel);

        string str = "SGMeshConstruct , pMesh->setupAggElement  at " + boost::lexical_cast<string>(imesh);
        mpLogger->Info(Utility::LoggerMode::MWDebug, str);
    };
}

// Refine for MultiGrid
// --
// current Mesh      => pMesh
// prolongation Mesh => pProgMesh
// 
// current CommMesh  => pCommMesh (通信領域メッシュ)
// prolongation CommMesh => pProgCommMesh
// 
void CMeshFactory::MGMeshConstruct()
{
    // AseeyModel & Mesh --
    CAssyModel *pAssy, *pProgAssy;
    CMesh      *pMesh, *pProgMesh;
    // Element
    CElement         *pElem=NULL;//元になった要素
    vector<CElement*> vProgElem; //分割された新しい要素達

    // 通信Mesh
    CCommMesh  *pCommMesh, *pProgCommMesh;
    // 通信要素(CommElem)
    CCommElement      *pCommElem;//元になったCommElem(親のCommElem)
    vector<CCommElement*> vProgCommElem;//生成されるprogCommElemのコンテナ
    
    uiint numOfCommMesh,numOfCommElemAll,numOfCommNode;
    uiint icommesh,icomelem,iprocom;
    

    // 階層”0”のMesh数を基準に各階層のAssyModelのMeshを決める.
    // ---
    pAssy= mpGMGModel->getAssyModel(0);
    uiint numOfMesh= pAssy->getNumOfMesh();
    
    uiint ilevel,imesh,ielem;
    // ---
    // 階層Level ループ
    // ---
    for(ilevel=0; ilevel< mMGLevel; ilevel++){
        
        pAssy= mpGMGModel->getAssyModel(ilevel);
        
        // prolongation AssyModel
        //
        pProgAssy= mpGMGModel->getAssyModel(ilevel+1);//FileReadRefineブロックでAssyModelは生成済み
        pProgAssy->resizeMesh(numOfMesh);

        pProgAssy->intializeBucket(pAssy->getMaxMeshID(),pAssy->getMinMeshID());//逆引き配列の領域確保
        pProgAssy->setMaxMeshID(pAssy->getMaxMeshID());
        pProgAssy->setMinMeshID(pAssy->getMinMeshID());
        // ---
        // Mesh(パーツ) ループ in AssyMode
        // ---
        for(imesh=0; imesh< numOfMesh; imesh++){

            pMesh= pAssy->getMesh(imesh);//Current_Level Mesh(最初期はLevel==0：ファイル読み込み時)
            pProgAssy->setBucket(pMesh->getMeshID(), imesh);//progAssyの逆引きに"id-index"をセット
            
            // <<<< start ::pProgMeshの生成処理 >>>>
            //
            pProgMesh = new CMesh;          //Upper_Level Mesh ==(prolongation Mesh)
            pProgMesh->setMGLevel(ilevel+1);//上位MeshのMultiGridレベルを設定(初期pMeshはファイル読み込み時のLevel==0)
            pProgMesh->setMeshID(pMesh->getMeshID());//Mesh_ID は,同一のIDとする.

            
            // Refineの準備, 頂点集合の要素と,辺-面-体積中心の節点生成(progMeshの節点生成),辺-面の要素集合
            //
            if(ilevel==0){
                // 条件(ilevel >= 1) は, pProgMesh->setupAggregate()をMesh-Refine後に行ってあるので不要:CommMeshのRefineの為.
                pMesh->setupAggregate(ilevel); //Node集合Element, Node集合Nodeの計算
                mpLogger->Info(Utility::LoggerMode::MWDebug,"pMesh->setupAggElement finish at ilevel==0");
            }
            pMesh->presetProgMesh(pProgMesh);//prolongation_Meshのノード,要素リザーブ(reserve) && pMeshのノード,要素をセット
            mpLogger->Info(Utility::LoggerMode::MWDebug,"pMesh->presetProgMesh finish");
            pMesh->setupEdgeElement(pProgMesh, ilevel);//辺(Edge)節点, 辺に集合する要素の計算
            mpLogger->Info(Utility::LoggerMode::MWDebug,"pMesh->setupEdgeElement finish");
            pMesh->setupFaceElement(pProgMesh);//面(Face)節点, 面に隣接する要素の計算
            mpLogger->Info(Utility::LoggerMode::MWDebug,"pMesh->setupFaceElement finish");
            pMesh->setupVolumeNode(pProgMesh); //体(Volume)節点:要素中心の節点
            mpLogger->Info(Utility::LoggerMode::MWDebug,"pMesh->setupVolumeNode finish");

            pMesh->replaceEdgeNode();//2次要素の場合、辺ノードを要素ノードとして移し替え

//            cout << "MeshFactory::MGMeshConstruct ---- A" << endl;
            
            // 新ElementID
            uiint numOfElem = pMesh->getNumOfElement();
            uiint elementID= 0;// 新たに生成される要素のIDを割振る. -> 各divid関数でカウントアップ.
                               // ElementのID(Index)初期値は,土台のMeshとは無関係 <= Nodeとは異なる.

            for(ielem=0; ielem< numOfElem; ielem++){
                pElem= pMesh->getElementIX(ielem);
                vProgElem.clear();
                
                GeneProgElem(ilevel, pElem, vProgElem, elementID, pProgMesh);//再分割要素の生成, 2010.5.31VC++同様に変更
            };

//            cout << "MeshFactory::MGMeshConstruct ---- B" << endl;
            
            // ノード, 数要素数のセット
            pProgMesh->setupNumOfNode();
            pProgMesh->setupNumOfElement();
            pProgMesh->setSolutionType(mnSolutionType);

//            cout << "MeshFactory::MGMeshConstruct ---- C" << endl;
            
            // pProgMeshの AggregateElement, AggregateNodeの生成
            // --
            uiint numOfNode= pProgMesh->getNodeSize();
            CAggregateElement *pAggElem;
            CAggregateNode    *pAggNode;
            pProgMesh->resizeAggregate(numOfNode);

            if(mnSolutionType==SolutionType::FEM){
                for(uiint iAgg=0; iAgg< numOfNode; iAgg++){
                    pAggElem = new CAggregateElement;
                    pProgMesh->setAggElement(pAggElem, iAgg);
                };
            }
            if(mnSolutionType==SolutionType::FVM){
                for(uiint iAgg=0; iAgg< numOfNode; iAgg++){
                    pAggNode = new CAggregateNode;
                    pProgMesh->setAggNode(pAggNode, iAgg);
                };
            }
            // !注意 CMesh::setupAggregate()は,このルーチンの先頭のRefine準備でpMeshに対してコールするのでpProgMeshにはコールしない.

//            cout << "MeshFactory::MGMeshConstruct ---- D" << endl;
            
            // prolongation AssyModelに,pProgMeshをセット
            //
            pProgAssy->setMesh(pProgMesh,pMesh->getMeshID());//progAssyModel に,progMeshをセット
            

            // progMeshのNode 逆引きセットアップ
            // --
            uiint progNodeSize = pProgMesh->getNodeSize();
            CNode *pNode = pProgMesh->getNodeIX(progNodeSize-1);
            uiint nProgMaxID = pNode->getID();
            pProgMesh->initBucketNode(nProgMaxID+1, pMesh->getMaxNodeID());//Bucketの領域確保
            pProgMesh->setupBucketNode();              //Bucketの"ID,Index"の一括処理

//            cout << "MeshFactory::MGMeshConstruct ---- E" << endl;

            // progMeshのElement 逆引きセットアップ
            // --
            uiint progElemSize = pProgMesh->getElementSize();
            pElem = pProgMesh->getElementIX(progElemSize-1);
            nProgMaxID = pElem->getID();
            pProgMesh->initBucketElement(nProgMaxID+1, 0);//要素は新規生成なのでMinID=0
            pProgMesh->setupBucketElement();

            // <<<< end ::pProgMeshの生成処理 >>>> //

//            cout << "MeshFactory::MGMeshConstruct ---- F" << endl;

            // Elementグループ
            //
            uiint iGrp, nNumOfGrp = pMesh->getNumOfElemGrp();
            CElementGroup *pElemGrp, *pProgElemGrp;
            for(iGrp=0; iGrp < nNumOfGrp; iGrp++){

                pProgElemGrp = new CElementGroup;

                pElemGrp = pMesh->getElemGrpIX(iGrp);

                pElemGrp->refine(pProgElemGrp);

                pProgElemGrp->setMesh(pProgMesh);
                pProgElemGrp->setID(pElemGrp->getID());
                pProgElemGrp->setName(pElemGrp->getName());

                pProgMesh->addElemGrp(pProgElemGrp);
            };

//            cout << "MeshFactory::MGMeshConstruct ---- G" << endl;

            // progCommMeshの前処理
            // --
            pProgMesh->setupAggregate(ilevel+1);

            ////////////////////////////////////////////////
            // <<<< start ::pProgCommCommMeshの生成処理 >>>>
            //
            numOfCommMesh= pMesh->getNumOfCommMesh();
            // --
            // CommMesh(通信Mesh)ループ in Mesh
            // --
            for(icommesh=0; icommesh< numOfCommMesh; icommesh++){
                pCommMesh= pMesh->getCommMesh(icommesh);

                // "new CommMesh"に,下段階層CommMeshのプロパティをセット
                // --
                CIndexBucket *pBucket= pProgMesh->getBucket();
                pProgCommMesh= new CCommMesh(pBucket);// <<<<<<<<<<<<<<-- prolongation CommMesh
                pProgCommMesh->setCommID( pCommMesh->getCommID());
                pProgCommMesh->setRankID( pCommMesh->getRankID());
                pProgCommMesh->setTransmitRankID( pCommMesh->getTransmitRankID());

                pProgMesh->setCommMesh(pProgCommMesh);//progMeshにprogCommMeshをセット


                numOfCommElemAll= pCommMesh->getNumOfCommElementAll();//カレントCommElement数
                pProgCommMesh->reserveCommElementAll(numOfCommElemAll*8);// <<<-- CommElemAllリザーブ:下段階層のCommElem数の8倍

                numOfCommNode= pCommMesh->getNumOfNode();//カレントCommMeshのNode数
                pProgCommMesh->reserveNode(numOfCommNode*8);// <<<-- ノード・リザーブ:下段階層のCommMeshのノード数の8倍
                // --
                // CommElementループ in CommMesh (カレントCommMesh)
                // --
                for(icomelem=0; icomelem< numOfCommElemAll; icomelem++){
                    //pCommMeshからCommElemを取得し,progCommElemを生成 => progCommMeshにセット, rankはCommMeshが所有
                    //
                    pCommElem= pCommMesh->getCommElementAll(icomelem);
                    pCommElem->setupProgNodeRank(ilevel+1);//辺,面,体積中心にRank設定 <<<<-- progCommElemのNodeランク

                    vProgCommElem.clear();
                    GeneProgCommElem(pCommElem, vProgCommElem);//<<<<<<-- prolongation CommElementの生成

                    // --
                    // progCommMeshへprogCommElemAllのセット(全てのCommElement)
                    // --
                    for(iprocom=0; iprocom< vProgCommElem.size(); iprocom++){
                        pProgCommMesh->setCommElementAll(vProgCommElem[iprocom]);
                    };
                };//CommElementループ・エンド

                //1.CommMesh内でのCommElemのIndex番号の割り振り && CommMesh内のCommElementを通信するか否かのCommElementに選別
                pProgCommMesh->AllocateCommElement();
                
                //2.CommMesh内でのCommElemの隣接情報
                //3.CommMesh内でのNodeのIndex番号の割り振り,CommMeshのmvNode,mvSendNode,mvRecvNodeの取得
                pProgCommMesh->setupAggCommElement(pProgMesh->getElements());
                pProgCommMesh->sortCommNodeIndex();// CommMesh内でのNode Index番号生成, Send,Recvノードの選別, DNode,DElementの選別ソート
                                                   // mvNodeのセットアップもsortCommNodeIndexから,CommElementのsetCommNodeIndex内でmvNodeにセットしている.
                //4.mapデータのセットアップ
                pProgCommMesh->setupMapID2CommID();

            };//CommMeshループ・エンド

//            cout << "MeshFactory::MGMeshConstruct ---- H" << endl;

            // Mesh のNode,Elementの計算領域整理:MeshのmvNode,mvElementから計算に使用しないNode(DNode),Element(DElement)を移動
            // --
            pProgMesh->sortMesh();

            // Meshが,ソートされたので,Bucketを再セットアップ
            pProgMesh->setupBucketNode();//Bucketの"ID,Index"の一括処理
            pProgMesh->setupBucketElement();
            //
            // <<<< end ::pProgCommCommMeshの生成処理 >>>>

//            cout << "MeshFactory::MGMeshConstruct ---- I" << endl;

        };//imesh ループ エンド
    };//ilevel ループ エンド
    
    //
    // 2次要素の場合：最終LevelのMeshに辺ノードをprogMeshに生成
    //
    pAssy= mpGMGModel->getAssyModel(mMGLevel);//最終LevelのAssyModel
    
    numOfMesh= pAssy->getNumOfMesh();
    for(imesh=0; imesh< numOfMesh; imesh++){
        pMesh= pAssy->getMesh(imesh);
        
        pMesh->setupEdgeElement(NULL, mMGLevel);//2次要素として利用するため,最終レベルのMeshに辺ノードを生成
        pMesh->replaceEdgeNode();//辺ノードを移し替え
    };
}

// 再分割要素(progElem)の生成 2010.05.31VC++同様に変更
//
void CMeshFactory::GeneProgElem(const uiint& ilevel,CElement* pElem, vector<CElement*>& vProgElem, uiint& elementID, CMesh* pProgMesh)
{
    CElement *pProgElem;
    uiint i;
    // divid Element(要素の分割)
    switch(pElem->getType()){
        case(ElementType::Hexa):
            vProgElem.reserve(8);//分割された新しい要素の生成
            for(i=0; i< 8; i++){
                pProgElem= new CHexa; pProgElem->setMGLevel(ilevel+1);
                pProgElem->initialize();

                vProgElem.push_back(pProgElem);
            };
            dividHexa(pElem,vProgElem, elementID, pProgMesh);
            break;

        case(ElementType::Hexa2):
            vProgElem.reserve(8);//分割された新しい要素の生成
            for(i=0; i< 8; i++){
                pProgElem= new CHexa2; pProgElem->setMGLevel(ilevel+1);
                pProgElem->initialize();

                vProgElem.push_back(pProgElem);
            };
            dividHexa(pElem,vProgElem, elementID, pProgMesh);
            break;

        case(ElementType::Tetra):
            vProgElem.reserve(4);//分割された新しい要素の生成
            for(i=0; i< 4; i++){
                pProgElem= new CHexa; pProgElem->setMGLevel(ilevel+1);
                pProgElem->initialize();

                vProgElem.push_back(pProgElem);
            };
            dividTetra(pElem,vProgElem, elementID, pProgMesh);
            break;

        case(ElementType::Tetra2):
            vProgElem.reserve(4);//分割された新しい要素の生成
            for(i=0; i< 4; i++){
                pProgElem= new CHexa2; pProgElem->setMGLevel(ilevel+1);
                pProgElem->initialize();
                
                vProgElem.push_back(pProgElem);
            };
            dividTetra(pElem,vProgElem, elementID, pProgMesh);
            break;

        case(ElementType::Prism):
            vProgElem.reserve(6);//分割された新しい要素の生成
            for(i=0; i< 6; i++){
                pProgElem= new CHexa; pProgElem->setMGLevel(ilevel+1);
                pProgElem->initialize();

                vProgElem.push_back(pProgElem);
            };
            dividPrism(pElem,vProgElem, elementID,pProgMesh);
            break;

        case(ElementType::Prism2):
            vProgElem.reserve(6);//分割された新しい要素の生成
            for(i=0; i< 6; i++){
                pProgElem= new CHexa2; pProgElem->setMGLevel(ilevel+1);
                pProgElem->initialize();
                
                vProgElem.push_back(pProgElem);
            };
            dividPrism(pElem,vProgElem, elementID,pProgMesh);
            break;

        case(ElementType::Quad):
            vProgElem.reserve(4);//分割された新しい要素の生成
            for(i=0; i< 4; i++){
                pProgElem= new CQuad; pProgElem->setMGLevel(ilevel+1);
                pProgElem->initialize();

                vProgElem.push_back(pProgElem);
            };
            dividQuad(pElem,vProgElem, elementID,pProgMesh);
            break;

        case(ElementType::Quad2):
            vProgElem.reserve(4);//分割された新しい要素の生成
            for(i=0; i< 4; i++){
                pProgElem= new CQuad2; pProgElem->setMGLevel(ilevel+1);
                pProgElem->initialize();

                vProgElem.push_back(pProgElem);
            };
            dividQuad(pElem,vProgElem, elementID,pProgMesh);
            break;

        case(ElementType::Triangle):
            vProgElem.reserve(3);//分割された新しい要素の生成
            for(i=0; i< 3; i++){
                pProgElem= new CQuad; pProgElem->setMGLevel(ilevel+1);
                pProgElem->initialize();

                vProgElem.push_back(pProgElem);
            };
            dividTriangle(pElem,vProgElem, elementID,pProgMesh);
            break;

        case(ElementType::Triangle2):
            vProgElem.reserve(3);//分割された新しい要素の生成
            for(i=0; i< 3; i++){
                pProgElem= new CQuad2; pProgElem->setMGLevel(ilevel+1);
                pProgElem->initialize();

                vProgElem.push_back(pProgElem);
            };
            dividTriangle(pElem,vProgElem, elementID,pProgMesh);
            break;

        case(ElementType::Beam):
            vProgElem.reserve(2);//分割された新しい要素の生成
            for(i=0; i< 2; i++){
                pProgElem= new CBeam; pProgElem->setMGLevel(ilevel+1);
                pProgElem->initialize();

                vProgElem.push_back(pProgElem);
            };
            dividBeam(pElem,vProgElem, elementID,pProgMesh);
            break;

        case(ElementType::Beam2):
            vProgElem.reserve(2);//分割された新しい要素の生成
            for(i=0; i< 2; i++){
                pProgElem= new CBeam2; pProgElem->setMGLevel(ilevel+1);
                pProgElem->initialize();
                
                vProgElem.push_back(pProgElem);
            };
            dividBeam(pElem,vProgElem, elementID,pProgMesh);
            break;
    }//switch エンド
}

// 要素を分割して生成, for refineMesh()
// --
// Hexa(6面体)の分割
//
void CMeshFactory::dividHexa(CElement* pElem, vector<CElement*>& vProgElem, uiint& elementID, CMesh* pProgMesh)
{
    vector<CNode*> vVertNode;//頂点のノード
    vector<CNode*> vEdgeNode;//辺のノード
    vector<CNode*> vFaceNode;//面のノード
    CNode          *pVolNode;//体中心のノード
    
    //    uint numOfVert,numOfEdge,numOfFace;//各要素の属性(分割の際に使用)
    //    numOfVert= NumberOfVertex::Hexa(); numOfFace= NumberOfFace::Hexa(); numOfEdge= NumberOfEdge::Hexa();

    uiint i;
    //頂点のノード
    vVertNode.resize(8); for(i=0; i< 8; i++){ vVertNode[i] = pElem->getNode(i);}
    //辺のノード
    vEdgeNode.resize(12);for(i=0; i< 12; i++){ vEdgeNode[i] = pElem->getEdgeInterNode(i);}
    //面のノード
    vFaceNode.resize(6); for(i=0; i< 6; i++){ vFaceNode[i] = pElem->getFaceNode(i);}
    //体ノード
    pVolNode = pElem->getVolumeNode();


    // 8個のHexaを生成
    // 要素 0
    vProgElem[0]->setNode(vVertNode[0],0); vProgElem[0]->setNode(vEdgeNode[0],1);
    vProgElem[0]->setNode(vFaceNode[0],2); vProgElem[0]->setNode(vEdgeNode[3],3);
    vProgElem[0]->setNode(vEdgeNode[8],4); vProgElem[0]->setNode(vFaceNode[4],5);
    vProgElem[0]->setNode(pVolNode,    6); vProgElem[0]->setNode(vFaceNode[3],7);

    pElem->setProgElem(vProgElem[0], 0);//CommElemのために頂点番号(vVertNode)順に親ElemにProgElemをセット

    // 要素 1
    vProgElem[1]->setNode(vEdgeNode[0],0); vProgElem[1]->setNode(vVertNode[1],1);
    vProgElem[1]->setNode(vEdgeNode[1],2); vProgElem[1]->setNode(vFaceNode[0],3);
    vProgElem[1]->setNode(vFaceNode[4],4); vProgElem[1]->setNode(vEdgeNode[9],5);
    vProgElem[1]->setNode(vFaceNode[2],6); vProgElem[1]->setNode(pVolNode,    7);

    pElem->setProgElem(vProgElem[1], 1);//CommElemのために頂点番号(vVertNode)順に親ElemにProgElemをセット

    // 要素 2
    vProgElem[2]->setNode(vEdgeNode[8],0); vProgElem[2]->setNode(vFaceNode[4],1);
    vProgElem[2]->setNode(pVolNode,    2); vProgElem[2]->setNode(vFaceNode[3],3);
    vProgElem[2]->setNode(vVertNode[4],4); vProgElem[2]->setNode(vEdgeNode[4],5);
    vProgElem[2]->setNode(vFaceNode[1],6); vProgElem[2]->setNode(vEdgeNode[7],7);

    pElem->setProgElem(vProgElem[2], 4);//CommElemのために頂点番号(vVertNode)順に親ElemにProgElemをセット

    // 要素 3
    vProgElem[3]->setNode(vFaceNode[4],0); vProgElem[3]->setNode(vEdgeNode[9],1);
    vProgElem[3]->setNode(vFaceNode[2],2); vProgElem[3]->setNode(pVolNode,    3);
    vProgElem[3]->setNode(vEdgeNode[4],4); vProgElem[3]->setNode(vVertNode[5],5);
    vProgElem[3]->setNode(vEdgeNode[5],6); vProgElem[3]->setNode(vFaceNode[1],7);
    
    pElem->setProgElem(vProgElem[3], 5);//CommElemのために頂点番号(vVertNode)順に親ElemにProgElemをセット

    // 要素 4
    vProgElem[4]->setNode(vEdgeNode[3],0); vProgElem[4]->setNode(vFaceNode[0],1);
    vProgElem[4]->setNode(vEdgeNode[2],2); vProgElem[4]->setNode(vVertNode[3],3);
    vProgElem[4]->setNode(vFaceNode[3],4); vProgElem[4]->setNode(pVolNode,    5);
    vProgElem[4]->setNode(vFaceNode[5],6); vProgElem[4]->setNode(vEdgeNode[11],7);
    
    pElem->setProgElem(vProgElem[4], 3);//CommElemのために頂点番号(vVertNode)順に親ElemにProgElemをセット

    // 要素 5
    vProgElem[5]->setNode(vFaceNode[0],0); vProgElem[5]->setNode(vEdgeNode[1],1);
    vProgElem[5]->setNode(vVertNode[2],2); vProgElem[5]->setNode(vEdgeNode[2],3);
    vProgElem[5]->setNode(pVolNode,    4); vProgElem[5]->setNode(vFaceNode[2],5);
    vProgElem[5]->setNode(vEdgeNode[10],6);vProgElem[5]->setNode(vFaceNode[5],7);
    
    pElem->setProgElem(vProgElem[5], 2);//CommElemのために頂点番号(vVertNode)順に親ElemにProgElemをセット

    // 要素 6
    vProgElem[6]->setNode(vFaceNode[3],0); vProgElem[6]->setNode(pVolNode,    1);
    vProgElem[6]->setNode(vFaceNode[5],2); vProgElem[6]->setNode(vEdgeNode[11],3);
    vProgElem[6]->setNode(vEdgeNode[7],4); vProgElem[6]->setNode(vFaceNode[1],5);
    vProgElem[6]->setNode(vEdgeNode[6],6); vProgElem[6]->setNode(vVertNode[7],7);
    
    pElem->setProgElem(vProgElem[6], 7);//CommElemのために頂点番号(vVertNode)順に親ElemにProgElemをセット

    // 要素 7
    vProgElem[7]->setNode(pVolNode,    0); vProgElem[7]->setNode(vFaceNode[2],1);
    vProgElem[7]->setNode(vEdgeNode[10],2); vProgElem[7]->setNode(vFaceNode[5],3);
    vProgElem[7]->setNode(vFaceNode[1],4); vProgElem[7]->setNode(vEdgeNode[5],5);
    vProgElem[7]->setNode(vVertNode[6],6); vProgElem[7]->setNode(vEdgeNode[6],7);
    
    pElem->setProgElem(vProgElem[7], 6);//CommElemのために頂点番号(vVertNode)順に親ElemにProgElemをセット

    
    // IDのセット
    for(i=0; i< 8; i++){
        //vProgElem[i]->setParentID(pElem->getID());//親の要素IDをParentIDにセット
        vProgElem[i]->setID(elementID);          // <= elementIDは,いままでの数を数えているので,直前の配列数
        ++elementID;//直前の番号を渡した後でelementIDをカウントアップ

        pProgMesh->setElement(vProgElem[i]);
    };

    // MPC面の属性セット
    uiint iface;
    //マスター面
    if(pElem->isMPCMaster()){
        for(iface=0; iface< 6; iface++){
            if(pElem->isMPCFace(iface)){
                switch(iface){
                    case(0):
                        setProgHexaMPCMaster(vProgElem, iface, 0,1,4,5);//Face_0 に生成される子要素:vProgElemの配列番号0,1,4,5
                        break;
                    case(1):
                        setProgHexaMPCMaster(vProgElem, iface, 2,3,6,7);//Face_1 に生成される子要素:vProgElemの配列番号2,3,6,7
                        break;
                    case(2):
                        setProgHexaMPCMaster(vProgElem, iface, 1,3,5,7);//Face_2 に生成される子要素:vProgElemの配列番号1,3,5,7
                        break;
                    case(3):
                        setProgHexaMPCMaster(vProgElem, iface, 0,2,4,6);//Face_3 に生成される子要素:vProgElemの配列番号0,2,4,6
                        break;
                    case(4):
                        setProgHexaMPCMaster(vProgElem, iface, 0,1,2,3);//Face_4 に生成される子要素:vProgElemの配列番号0,1,2,3
                        break;
                    case(5):
                        setProgHexaMPCMaster(vProgElem, iface, 4,5,6,7);//Face_5 に生成される子要素:vProgElemの配列番号4,5,6,7
                        break;
                }
            }
        };
    }
    //スレーブ面
    if(pElem->isMPCSlave()){
        for(iface=0; iface< 6; iface++){
            if(pElem->isMPCFace(iface)){
                switch(iface){
                    case(0):
                        setProgHexaMPCSlave(vProgElem, iface, 0,1,4,5);
                        break;
                    case(1):
                        setProgHexaMPCSlave(vProgElem, iface, 2,3,6,7);
                        break;
                    case(2):
                        setProgHexaMPCSlave(vProgElem, iface, 1,3,5,7);
                        break;
                    case(3):
                        setProgHexaMPCSlave(vProgElem, iface, 0,2,4,6);
                        break;
                    case(4):
                        setProgHexaMPCSlave(vProgElem, iface, 0,1,2,3);
                        break;
                    case(5):
                        setProgHexaMPCSlave(vProgElem, iface, 4,5,6,7);
                        break;
                }
            }
        };
    }
    
    //CommMesh2(節点通信界面)
    if(pElem->isCommMesh2()){
        for(iface=0; iface< 6; iface++){
            if(pElem->isCommEntity(iface)){
                switch(iface){
                    case(0):
                        setProgHexaCommMesh2Entity(vProgElem, iface, 0,1,4,5);//Face_0 に生成される子要素:vProgElemの配列番号0,1,4,5
                        break;
                    case(1):
                        setProgHexaCommMesh2Entity(vProgElem, iface, 2,3,6,7);//Face_1 に生成される子要素:vProgElemの配列番号2,3,6,7
                        break;
                    case(2):
                        setProgHexaCommMesh2Entity(vProgElem, iface, 1,3,5,7);//Face_2 に生成される子要素:vProgElemの配列番号1,3,5,7
                        break;
                    case(3):
                        setProgHexaCommMesh2Entity(vProgElem, iface, 0,2,4,6);//Face_3 に生成される子要素:vProgElemの配列番号0,2,4,6
                        break;
                    case(4):
                        setProgHexaCommMesh2Entity(vProgElem, iface, 0,1,2,3);//Face_4 に生成される子要素:vProgElemの配列番号0,1,2,3
                        break;
                    case(5):
                        setProgHexaCommMesh2Entity(vProgElem, iface, 4,5,6,7);//Face_5 に生成される子要素:vProgElemの配列番号4,5,6,7
                        break;
                }
            }
        };
    }
    
}

// MPC面の属性をprogElemにセット
// --
// マスター
void CMeshFactory::setProgHexaMPCMaster(vector<CElement*>& vProgElem, const uiint& iface, const uiint& i, const uiint& j, const uiint& k, const uiint& l)
{
    vProgElem[i]->markingMPCMaster(); vProgElem[i]->markingMPCFace(iface);
    vProgElem[j]->markingMPCMaster(); vProgElem[j]->markingMPCFace(iface);
    vProgElem[k]->markingMPCMaster(); vProgElem[k]->markingMPCFace(iface);
    vProgElem[l]->markingMPCMaster(); vProgElem[l]->markingMPCFace(iface);
}
// スレーブ
void CMeshFactory::setProgHexaMPCSlave(vector<CElement*>& vProgElem, const uiint& iface, const uiint& i, const uiint& j, const uiint& k, const uiint& l)
{
    vProgElem[i]->markingMPCSlave(); vProgElem[i]->markingMPCFace(iface);
    vProgElem[j]->markingMPCSlave(); vProgElem[j]->markingMPCFace(iface);
    vProgElem[k]->markingMPCSlave(); vProgElem[k]->markingMPCFace(iface);
    vProgElem[l]->markingMPCSlave(); vProgElem[l]->markingMPCFace(iface);
}

// CommMesh2(節点通信界面)の属性をprogElemにセット
// --
void CMeshFactory::setProgHexaCommMesh2Entity(vector<CElement*>& vProgElem, const uiint& iface, const uiint& i, const uiint& j, const uiint& k, const uiint& l)
{
    vProgElem[i]->markingCommMesh2(); vProgElem[i]->markingCommEntity(iface);
    vProgElem[j]->markingCommMesh2(); vProgElem[j]->markingCommEntity(iface);
    vProgElem[k]->markingCommMesh2(); vProgElem[k]->markingCommEntity(iface);
    vProgElem[l]->markingCommMesh2(); vProgElem[l]->markingCommEntity(iface);
}

// 4面体の分割
//
void CMeshFactory::dividTetra(CElement* pElem, vector<CElement*>& vProgElem, uiint& indexCount, CMesh* pProgMesh)
{
    vector<CNode*> vVertNode;//頂点のノード
    vector<CNode*> vEdgeNode;//辺のノード
    vector<CNode*> vFaceNode;//面のノード
    CNode          *pVolNode;//体中心のノード
    
    uiint numOfVert,numOfEdge,numOfFace;//各要素の属性(分割の際に使用)
    numOfVert= NumberOfVertex::Tetra(); numOfFace= NumberOfFace::Tetra(); numOfEdge= NumberOfEdge::Tetra();

    uiint i;
    //頂点のノード
    vVertNode.resize(numOfVert);
    for(i=0; i< numOfVert; i++){
        vVertNode[i] = pElem->getNode(i);
    }
    //辺のノード
    vEdgeNode.resize(numOfEdge);
    for(i=0; i< numOfEdge; i++){
        vEdgeNode[i] = pElem->getEdgeInterNode(i);
    }
    //面のノード
    vFaceNode.resize(numOfFace);
    for(i=0; i< numOfFace; i++){
        vFaceNode[i] = pElem->getFaceNode(i);
    }
    //体ノード
    pVolNode = pElem->getVolumeNode();


    // 4個のHexaを生成
    // --
    // 要素 0
    vProgElem[0]->setNode(vEdgeNode[2],0); vProgElem[0]->setNode(vVertNode[0],1);
    vProgElem[0]->setNode(vEdgeNode[0],2); vProgElem[0]->setNode(vFaceNode[0],3);
    vProgElem[0]->setNode(vFaceNode[3],4); vProgElem[0]->setNode(vEdgeNode[3],5);
    vProgElem[0]->setNode(vFaceNode[1],6); vProgElem[0]->setNode(pVolNode,    7);
    
    pElem->setProgElem(vProgElem[0], 0);//CommElemのために頂点番号(vVertNode)順に親ElemにProgElemをセット
    
    // 要素 1
    vProgElem[1]->setNode(vVertNode[2],0); vProgElem[1]->setNode(vEdgeNode[2],1);
    vProgElem[1]->setNode(vFaceNode[0],2); vProgElem[1]->setNode(vEdgeNode[1],3);
    vProgElem[1]->setNode(vEdgeNode[5],4); vProgElem[1]->setNode(vFaceNode[3],5);
    vProgElem[1]->setNode(pVolNode,    6); vProgElem[1]->setNode(vFaceNode[2],7);
    
    pElem->setProgElem(vProgElem[1], 2);
    
    // 要素 2
    vProgElem[2]->setNode(vFaceNode[0],0); vProgElem[2]->setNode(vEdgeNode[0],1);
    vProgElem[2]->setNode(vVertNode[1],2); vProgElem[2]->setNode(vEdgeNode[1],3);
    vProgElem[2]->setNode(pVolNode,    4); vProgElem[2]->setNode(vFaceNode[1],5);
    vProgElem[2]->setNode(vEdgeNode[4],6); vProgElem[2]->setNode(vFaceNode[2],7);
    
    pElem->setProgElem(vProgElem[2], 1);
    
    // 要素 3
    vProgElem[3]->setNode(vFaceNode[3],0); vProgElem[3]->setNode(vEdgeNode[3],1);
    vProgElem[3]->setNode(vFaceNode[1],2); vProgElem[3]->setNode(pVolNode,    3);
    vProgElem[3]->setNode(vEdgeNode[5],4); vProgElem[3]->setNode(vVertNode[3],5);
    vProgElem[3]->setNode(vEdgeNode[4],6); vProgElem[3]->setNode(vFaceNode[2],7);
    
    pElem->setProgElem(vProgElem[3], 3);

    ////debug
    //cout << "要素にノードをセット@dividTetra" << endl;

    // IDのセット
    for(i=0; i< 4; i++){
        //vProgElem[i]->setParentID(pElem->getID());//親の要素IDをParentIDにセット

        vProgElem[i]->setID(indexCount);//配列Indexは直前の配列数
        ++indexCount;

        pProgMesh->setElement(vProgElem[i]);
    };

    // MPC面の属性セット
    uiint iface;
    //マスター面
    if(pElem->isMPCMaster()){
        for(iface=0; iface< 4; iface++){
            if(pElem->isMPCFace(iface)){
                switch(iface){
                    case(0):
                        vProgElem[0]->markingMPCMaster(); vProgElem[0]->markingMPCFace(0);
                        vProgElem[1]->markingMPCMaster(); vProgElem[1]->markingMPCFace(0);
                        vProgElem[2]->markingMPCMaster(); vProgElem[2]->markingMPCFace(0);
                        break;
                    case(1):
                        vProgElem[0]->markingMPCMaster(); vProgElem[0]->markingMPCFace(2);
                        vProgElem[2]->markingMPCMaster(); vProgElem[2]->markingMPCFace(2);
                        vProgElem[3]->markingMPCMaster(); vProgElem[3]->markingMPCFace(2);
                        break;
                    case(2):
                        vProgElem[1]->markingMPCMaster(); vProgElem[1]->markingMPCFace(3);
                        vProgElem[2]->markingMPCMaster(); vProgElem[2]->markingMPCFace(5);
                        vProgElem[3]->markingMPCMaster(); vProgElem[3]->markingMPCFace(1);
                        break;
                    case(3):
                        vProgElem[0]->markingMPCMaster(); vProgElem[0]->markingMPCFace(4);
                        vProgElem[1]->markingMPCMaster(); vProgElem[1]->markingMPCFace(4);
                        vProgElem[3]->markingMPCMaster(); vProgElem[3]->markingMPCFace(4);
                        break;
                }
            }
        };
    }
    //スレーブ面
    if(pElem->isMPCSlave()){
        for(iface=0; iface< 4; iface++){
            if(pElem->isMPCFace(iface)){
                switch(iface){
                    case(0):
                        vProgElem[0]->markingMPCSlave(); vProgElem[0]->markingMPCFace(0);
                        vProgElem[1]->markingMPCSlave(); vProgElem[1]->markingMPCFace(0);
                        vProgElem[2]->markingMPCSlave(); vProgElem[2]->markingMPCFace(0);
                        break;
                    case(1):
                        vProgElem[0]->markingMPCSlave(); vProgElem[0]->markingMPCFace(2);
                        vProgElem[2]->markingMPCSlave(); vProgElem[2]->markingMPCFace(2);
                        vProgElem[3]->markingMPCSlave(); vProgElem[3]->markingMPCFace(2);
                        break;
                    case(2):
                        vProgElem[1]->markingMPCSlave(); vProgElem[1]->markingMPCFace(3);
                        vProgElem[2]->markingMPCSlave(); vProgElem[2]->markingMPCFace(5);
                        vProgElem[3]->markingMPCSlave(); vProgElem[3]->markingMPCFace(1);
                        break;
                    case(3):
                        vProgElem[0]->markingMPCSlave(); vProgElem[0]->markingMPCFace(4);
                        vProgElem[1]->markingMPCSlave(); vProgElem[1]->markingMPCFace(4);
                        vProgElem[3]->markingMPCSlave(); vProgElem[3]->markingMPCFace(4);
                        break;
                }
            }
        };
    }

    //CommMesh2(節点通信界面)
    if(pElem->isCommMesh2()){
        for(iface=0; iface< 4; iface++){
            if(pElem->isCommEntity(iface)){
                switch(iface){
                    case(0):
                        vProgElem[0]->markingCommMesh2(); vProgElem[0]->markingCommEntity(0);
                        vProgElem[1]->markingCommMesh2(); vProgElem[1]->markingCommEntity(0);
                        vProgElem[2]->markingCommMesh2(); vProgElem[2]->markingCommEntity(0);
                        break;
                    case(1):
                        vProgElem[0]->markingCommMesh2(); vProgElem[0]->markingCommEntity(2);
                        vProgElem[2]->markingCommMesh2(); vProgElem[2]->markingCommEntity(2);
                        vProgElem[3]->markingCommMesh2(); vProgElem[3]->markingCommEntity(2);
                        break;
                    case(2):
                        vProgElem[1]->markingCommMesh2(); vProgElem[1]->markingCommEntity(3);
                        vProgElem[2]->markingCommMesh2(); vProgElem[2]->markingCommEntity(5);
                        vProgElem[3]->markingCommMesh2(); vProgElem[3]->markingCommEntity(1);
                        break;
                    case(3):
                        vProgElem[0]->markingCommMesh2(); vProgElem[0]->markingCommEntity(4);
                        vProgElem[1]->markingCommMesh2(); vProgElem[1]->markingCommEntity(4);
                        vProgElem[3]->markingCommMesh2(); vProgElem[3]->markingCommEntity(4);
                        break;
                }
            }
        };
    }
}


// プリズムの分割
//
void CMeshFactory::dividPrism(CElement* pElem, vector<CElement*>& vProgElem, uiint& indexCount, CMesh* pProgMesh)
{
    vector<CNode*> vVertNode;//頂点のノード
    vector<CNode*> vEdgeNode;//辺のノード
    vector<CNode*> vFaceNode;//面のノード
    CNode          *pVolNode;//体中心のノード
    
    uiint numOfVert,numOfEdge,numOfFace;//各要素の属性(分割の際に使用)
    numOfVert= NumberOfVertex::Prism(); numOfFace= NumberOfFace::Prism(); numOfEdge= NumberOfEdge::Prism();

    uiint i;
    //頂点のノード
    vVertNode.resize(numOfVert); for(i=0; i< numOfVert; i++){ vVertNode[i] = pElem->getNode(i);}
    //辺のノード
    vEdgeNode.resize(numOfEdge);for(i=0; i< numOfEdge; i++){ vEdgeNode[i] = pElem->getEdgeInterNode(i);}
    //面のノード
    vFaceNode.resize(numOfFace); for(i=0; i< numOfFace; i++){ vFaceNode[i] = pElem->getFaceNode(i);}
    //体ノード
    pVolNode = pElem->getVolumeNode();
    
    // 要素 0
    vProgElem[0]->setNode(vVertNode[2],0); vProgElem[0]->setNode(vEdgeNode[1],1);
    vProgElem[0]->setNode(vFaceNode[0],2); vProgElem[0]->setNode(vEdgeNode[2],3);
    vProgElem[0]->setNode(vEdgeNode[5],4); vProgElem[0]->setNode(vFaceNode[4],5);
    vProgElem[0]->setNode(pVolNode,    6); vProgElem[0]->setNode(vFaceNode[3],7);
    
    pElem->setProgElem(vProgElem[0], 2);//CommElemのために頂点番号(vVertNode)順に親ElemにProgElemをセット
    
    // 要素 1
    vProgElem[1]->setNode(vEdgeNode[1],0); vProgElem[1]->setNode(vVertNode[0],1);
    vProgElem[1]->setNode(vEdgeNode[0],2); vProgElem[1]->setNode(vFaceNode[0],3);
    vProgElem[1]->setNode(vFaceNode[4],4); vProgElem[1]->setNode(vEdgeNode[3],5);
    vProgElem[1]->setNode(vFaceNode[2],6); vProgElem[1]->setNode(pVolNode,    7);
    
    pElem->setProgElem(vProgElem[1], 0);
    
    // 要素 2
    vProgElem[2]->setNode(vFaceNode[0],0); vProgElem[2]->setNode(vEdgeNode[0],1);
    vProgElem[2]->setNode(vVertNode[1],2); vProgElem[2]->setNode(vEdgeNode[2],3);
    vProgElem[2]->setNode(pVolNode,    4); vProgElem[2]->setNode(vFaceNode[2],5);
    vProgElem[2]->setNode(vEdgeNode[4],6); vProgElem[2]->setNode(vFaceNode[3],7);
    
    pElem->setProgElem(vProgElem[2], 1);

    // 要素 3
    vProgElem[3]->setNode(vEdgeNode[5],0); vProgElem[3]->setNode(vFaceNode[4],1);
    vProgElem[3]->setNode(pVolNode,    2); vProgElem[3]->setNode(vFaceNode[3],3);
    vProgElem[3]->setNode(vVertNode[5],4); vProgElem[3]->setNode(vEdgeNode[8],5);
    vProgElem[3]->setNode(vFaceNode[1],6); vProgElem[3]->setNode(vEdgeNode[7],7);
    
    pElem->setProgElem(vProgElem[3], 5);
    
    // 要素 4
    vProgElem[4]->setNode(vFaceNode[4],0); vProgElem[4]->setNode(vEdgeNode[3],1);
    vProgElem[4]->setNode(vFaceNode[2],2); vProgElem[4]->setNode(pVolNode,    3);
    vProgElem[4]->setNode(vEdgeNode[8],4); vProgElem[4]->setNode(vVertNode[3],5);
    vProgElem[4]->setNode(vEdgeNode[6],6); vProgElem[4]->setNode(vFaceNode[1],7);
    
    pElem->setProgElem(vProgElem[4], 3);
    
    // 要素 5
    vProgElem[5]->setNode(pVolNode,    0); vProgElem[5]->setNode(vFaceNode[2],1);
    vProgElem[5]->setNode(vEdgeNode[4],2); vProgElem[5]->setNode(vFaceNode[3],3);
    vProgElem[5]->setNode(vFaceNode[1],4); vProgElem[5]->setNode(vEdgeNode[6],5);
    vProgElem[5]->setNode(vVertNode[4],6); vProgElem[5]->setNode(vEdgeNode[7],7);
    
    pElem->setProgElem(vProgElem[5], 4);

    // IDのセット
    for(i=0; i< 6; i++){
        //vProgElem[i]->setParentID(pElem->getID());//親の要素IDをParentIDにセット

        vProgElem[i]->setID(indexCount);//配列Indexは直前の配列数
        ++indexCount;

        pProgMesh->setElement(vProgElem[i]);
    };

    // MPC面の属性セット
    uiint iface;
    //マスター面
    if(pElem->isMPCMaster()){
        for(iface=0; iface< 5; iface++){
            if(pElem->isMPCFace(iface)){
                switch(iface){
                    case(0):
                        vProgElem[0]->markingMPCMaster(); vProgElem[0]->markingMPCFace(0);
                        vProgElem[1]->markingMPCMaster(); vProgElem[1]->markingMPCFace(0);
                        vProgElem[2]->markingMPCMaster(); vProgElem[2]->markingMPCFace(0);
                        break;
                    case(1):
                        vProgElem[3]->markingMPCMaster(); vProgElem[3]->markingMPCFace(1);
                        vProgElem[4]->markingMPCMaster(); vProgElem[4]->markingMPCFace(1);
                        vProgElem[5]->markingMPCMaster(); vProgElem[5]->markingMPCFace(1);
                        break;
                    case(2):
                        vProgElem[1]->markingMPCMaster(); vProgElem[1]->markingMPCFace(2);
                        vProgElem[2]->markingMPCMaster(); vProgElem[2]->markingMPCFace(2);
                        vProgElem[4]->markingMPCMaster(); vProgElem[4]->markingMPCFace(2);
                        vProgElem[5]->markingMPCMaster(); vProgElem[5]->markingMPCFace(2);
                        break;
                    case(3):
                        vProgElem[0]->markingMPCMaster(); vProgElem[0]->markingMPCFace(3);
                        vProgElem[2]->markingMPCMaster(); vProgElem[2]->markingMPCFace(5);
                        vProgElem[3]->markingMPCMaster(); vProgElem[3]->markingMPCFace(3);
                        vProgElem[5]->markingMPCMaster(); vProgElem[5]->markingMPCFace(5);
                        break;
                    case(4):
                        vProgElem[0]->markingMPCMaster(); vProgElem[0]->markingMPCFace(4);
                        vProgElem[1]->markingMPCMaster(); vProgElem[1]->markingMPCFace(4);
                        vProgElem[3]->markingMPCMaster(); vProgElem[3]->markingMPCFace(4);
                        vProgElem[4]->markingMPCMaster(); vProgElem[4]->markingMPCFace(4);
                        break;
                }
            }
        };
    }
    //スレーブ面
    if(pElem->isMPCSlave()){
        for(iface=0; iface< 5; iface++){
            if(pElem->isMPCFace(iface)){
                switch(iface){
                    case(0):
                        vProgElem[0]->markingMPCSlave(); vProgElem[0]->markingMPCFace(0);
                        vProgElem[1]->markingMPCSlave(); vProgElem[1]->markingMPCFace(0);
                        vProgElem[2]->markingMPCSlave(); vProgElem[2]->markingMPCFace(0);
                        break;
                    case(1):
                        vProgElem[3]->markingMPCSlave(); vProgElem[3]->markingMPCFace(1);
                        vProgElem[4]->markingMPCSlave(); vProgElem[4]->markingMPCFace(1);
                        vProgElem[5]->markingMPCSlave(); vProgElem[5]->markingMPCFace(1);
                        break;
                    case(2):
                        vProgElem[1]->markingMPCSlave(); vProgElem[1]->markingMPCFace(2);
                        vProgElem[2]->markingMPCSlave(); vProgElem[2]->markingMPCFace(2);
                        vProgElem[4]->markingMPCSlave(); vProgElem[4]->markingMPCFace(2);
                        vProgElem[5]->markingMPCSlave(); vProgElem[5]->markingMPCFace(2);
                        break;
                    case(3):
                        vProgElem[0]->markingMPCSlave(); vProgElem[0]->markingMPCFace(3);
                        vProgElem[2]->markingMPCSlave(); vProgElem[2]->markingMPCFace(5);
                        vProgElem[3]->markingMPCSlave(); vProgElem[3]->markingMPCFace(3);
                        vProgElem[5]->markingMPCSlave(); vProgElem[5]->markingMPCFace(5);
                        break;
                    case(4):
                        vProgElem[0]->markingMPCSlave(); vProgElem[0]->markingMPCFace(4);
                        vProgElem[1]->markingMPCSlave(); vProgElem[1]->markingMPCFace(4);
                        vProgElem[3]->markingMPCSlave(); vProgElem[3]->markingMPCFace(4);
                        vProgElem[4]->markingMPCSlave(); vProgElem[4]->markingMPCFace(4);
                        break;
                }
            }
        };
    }

    //CommMesh2(節点通信界面)
    if(pElem->isCommMesh2()){
        for(iface=0; iface< 5; iface++){
            if(pElem->isCommEntity(iface)){
                switch(iface){
                    case(0):
                        vProgElem[0]->markingCommMesh2(); vProgElem[0]->markingCommEntity(0);
                        vProgElem[1]->markingCommMesh2(); vProgElem[1]->markingCommEntity(0);
                        vProgElem[2]->markingCommMesh2(); vProgElem[2]->markingCommEntity(0);
                        break;
                    case(1):
                        vProgElem[3]->markingCommMesh2(); vProgElem[3]->markingCommEntity(1);
                        vProgElem[4]->markingCommMesh2(); vProgElem[4]->markingCommEntity(1);
                        vProgElem[5]->markingCommMesh2(); vProgElem[5]->markingCommEntity(1);
                        break;
                    case(2):
                        vProgElem[1]->markingCommMesh2(); vProgElem[1]->markingCommEntity(2);
                        vProgElem[2]->markingCommMesh2(); vProgElem[2]->markingCommEntity(2);
                        vProgElem[4]->markingCommMesh2(); vProgElem[4]->markingCommEntity(2);
                        vProgElem[5]->markingCommMesh2(); vProgElem[5]->markingCommEntity(2);
                        break;
                    case(3):
                        vProgElem[0]->markingCommMesh2(); vProgElem[0]->markingCommEntity(3);
                        vProgElem[2]->markingCommMesh2(); vProgElem[2]->markingCommEntity(5);
                        vProgElem[3]->markingCommMesh2(); vProgElem[3]->markingCommEntity(3);
                        vProgElem[5]->markingCommMesh2(); vProgElem[5]->markingCommEntity(5);
                        break;
                    case(4):
                        vProgElem[0]->markingCommMesh2(); vProgElem[0]->markingCommEntity(4);
                        vProgElem[1]->markingCommMesh2(); vProgElem[1]->markingCommEntity(4);
                        vProgElem[3]->markingCommMesh2(); vProgElem[3]->markingCommEntity(4);
                        vProgElem[4]->markingCommMesh2(); vProgElem[4]->markingCommEntity(4);
                        break;
                }
            }
        };
    }
}
// ピラミッドの分割 <= 削除
//
void CMeshFactory::dividPyramid(CElement* pElem, vector<CElement*>& vProgElem, uiint& indexCount, CMesh* pProgMesh)
{
    vector<CNode*> vVertNode;//頂点のノード
    vector<CNode*> vEdgeNode;//辺のノード
    vector<CNode*> vFaceNode;//面のノード
    CNode          *pVolNode;//体中心のノード
    
    uiint numOfVert,numOfEdge,numOfFace;//各要素の属性(分割の際に使用)
    numOfVert= NumberOfVertex::Pyramid(); numOfFace= NumberOfFace::Pyramid(); numOfEdge= NumberOfEdge::Pyramid();

    uiint i;
    //頂点のノード
    vVertNode.resize(numOfVert); for(i=0; i< numOfVert; i++){ vVertNode[i] = pElem->getNode(i);}
    //辺のノード
    vEdgeNode.resize(numOfEdge);for(i=0; i< numOfEdge; i++){ vEdgeNode[i] = pElem->getEdgeInterNode(i);}
    //面のノード
    vFaceNode.resize(numOfFace); for(i=0; i< numOfFace; i++){ vFaceNode[i] = pElem->getFaceNode(i);}
    //体ノード
    pVolNode = pElem->getVolumeNode();
    
    // 要素 0
    vProgElem[0]->setNode(vVertNode[0],0); vProgElem[0]->setNode(vEdgeNode[0],1);
    vProgElem[0]->setNode(vFaceNode[0],2); vProgElem[0]->setNode(vEdgeNode[3],3);
    vProgElem[0]->setNode(vEdgeNode[7],4); vProgElem[0]->setNode(vFaceNode[4],5);
    vProgElem[0]->setNode(pVolNode,    6); vProgElem[0]->setNode(vFaceNode[3],7);
    
    pElem->setProgElem(vProgElem[0], 0);//CommElemのために頂点番号(vVertNode)順に親ElemにProgElemをセット
    
    // 要素 1
    vProgElem[1]->setNode(vEdgeNode[0],0); vProgElem[1]->setNode(vVertNode[1],1);
    vProgElem[1]->setNode(vEdgeNode[1],2); vProgElem[1]->setNode(vFaceNode[0],3);
    vProgElem[1]->setNode(vFaceNode[4],4); vProgElem[1]->setNode(vEdgeNode[4],5);
    vProgElem[1]->setNode(vFaceNode[1],6); vProgElem[1]->setNode(pVolNode,    7);
    
    pElem->setProgElem(vProgElem[1], 1);
    
    // 要素 2
    vProgElem[2]->setNode(vFaceNode[0],0); vProgElem[2]->setNode(vEdgeNode[1],1);
    vProgElem[2]->setNode(vVertNode[2],2); vProgElem[2]->setNode(vEdgeNode[2],3);
    vProgElem[2]->setNode(pVolNode,    4); vProgElem[2]->setNode(vFaceNode[1],5);
    vProgElem[2]->setNode(vEdgeNode[5],6); vProgElem[2]->setNode(vFaceNode[2],7);
    
    pElem->setProgElem(vProgElem[2], 2);
    
    // 要素 3
    vProgElem[3]->setNode(vEdgeNode[3],0); vProgElem[3]->setNode(vFaceNode[0],1);
    vProgElem[3]->setNode(vEdgeNode[2],2); vProgElem[3]->setNode(vVertNode[3],3);
    vProgElem[3]->setNode(vFaceNode[3],4); vProgElem[3]->setNode(pVolNode,    5);
    vProgElem[3]->setNode(vFaceNode[2],6); vProgElem[3]->setNode(vEdgeNode[6],7);
    
    pElem->setProgElem(vProgElem[3], 3);
    

//    // 要素 4 (pyramid)
//    vProgElem[4]->setNode(vEdgeNode[7],0); vProgElem[4]->setNode(vFaceNode[4],1);
//    vProgElem[4]->setNode(pVolNode,    2); vProgElem[4]->setNode(vFaceNode[3],3);
//    vProgElem[4]->setNode(vVertNode[4],4);
//    // 要素 5 (pyramid)
//    vProgElem[5]->setNode(vFaceNode[4],0); vProgElem[5]->setNode(vEdgeNode[4],1);
//    vProgElem[5]->setNode(vFaceNode[1],2); vProgElem[5]->setNode(pVolNode,    3);
//    vProgElem[5]->setNode(vVertNode[4],4);
//    // 要素 6 (pyramid)
//    vProgElem[6]->setNode(vFaceNode[1],0); vProgElem[6]->setNode(vEdgeNode[5],1);
//    vProgElem[6]->setNode(vFaceNode[2],2); vProgElem[6]->setNode(pVolNode,    3);
//    vProgElem[6]->setNode(vVertNode[4],4);
//    // 要素 7 (pyramid)
//    vProgElem[7]->setNode(vFaceNode[2],0); vProgElem[7]->setNode(vEdgeNode[6],1);
//    vProgElem[7]->setNode(vFaceNode[3],2); vProgElem[7]->setNode(pVolNode,    3);
//    vProgElem[7]->setNode(vVertNode[4],4);


    // 要素 4 (pyramid)
    vProgElem[4]->setNode(vEdgeNode[7],0); vProgElem[4]->setNode(vVertNode[4],1);
    vProgElem[4]->setNode(vEdgeNode[4],2); vProgElem[4]->setNode(vFaceNode[4],3);
    vProgElem[4]->setNode(pVolNode,    4);
    
    // pyramid分割されたProgElemは全て頂点4を所有するので,
    // 頂点番号によるProgElemの分類に加えて,Face順に数える.
    pElem->setProgElem(vProgElem[4], 7);//FaceNode[4]
    
    // 要素 5 (pyramid)
    vProgElem[5]->setNode(vEdgeNode[4],0); vProgElem[5]->setNode(vVertNode[4],1);
    vProgElem[5]->setNode(vEdgeNode[5],2); vProgElem[5]->setNode(vFaceNode[1],3);
    vProgElem[5]->setNode(pVolNode,    4);
    
    pElem->setProgElem(vProgElem[5], 4);//FaceNode[1]
    
    // 要素 6 (pyramid)
    vProgElem[6]->setNode(vEdgeNode[5],0); vProgElem[6]->setNode(vVertNode[4],1);
    vProgElem[6]->setNode(vEdgeNode[6],2); vProgElem[6]->setNode(vFaceNode[2],3);
    vProgElem[6]->setNode(pVolNode,    4);
    
    pElem->setProgElem(vProgElem[6], 5);//FaceNode[2]
    
    // 要素 7 (pyramid)
    vProgElem[7]->setNode(vEdgeNode[6],0); vProgElem[7]->setNode(vVertNode[4],1);
    vProgElem[7]->setNode(vEdgeNode[7],2); vProgElem[7]->setNode(vFaceNode[3],3);
    vProgElem[7]->setNode(pVolNode,    4);
    
    pElem->setProgElem(vProgElem[7], 6);//FaceNode[3]

    // IDのセット
    for(i=0; i< 8; i++){
        //vProgElem[i]->setParentID(pElem->getID());//親の要素IDをParentIDにセット
        
        vProgElem[i]->setID(indexCount);//配列Indexは直前の配列数
        ++indexCount;

        pProgMesh->setElement(vProgElem[i]);
    };

    //MPCは実装しない=> ピラミッドは削除予定

}
// 四辺形の分割
//
void CMeshFactory::dividQuad(CElement* pElem, vector<CElement*>& vProgElem, uiint& indexCount, CMesh* pProgMesh)
{
    vector<CNode*> vVertNode;//頂点のノード
    vector<CNode*> vEdgeNode;//辺のノード
    vector<CNode*> vFaceNode;//面のノード
    
    uiint numOfVert,numOfEdge,numOfFace;//各要素の属性(分割の際に使用)
    numOfVert= NumberOfVertex::Quad(); numOfFace= NumberOfFace::Quad(); numOfEdge= NumberOfEdge::Quad();

    uiint i;
    //頂点のノード
    vVertNode.resize(numOfVert); for(i=0; i< numOfVert; i++){ vVertNode[i] = pElem->getNode(i);}
    //辺のノード
    vEdgeNode.resize(numOfEdge);for(i=0; i< numOfEdge; i++){ vEdgeNode[i] = pElem->getEdgeInterNode(i);}
    //面のノード
    vFaceNode.resize(numOfFace); for(i=0; i< numOfFace; i++){ vFaceNode[i] = pElem->getFaceNode(i);}
    
    // 要素 0
    vProgElem[0]->setNode(vVertNode[0],0); vProgElem[0]->setNode(vEdgeNode[0],1);
    vProgElem[0]->setNode(vFaceNode[0],2); vProgElem[0]->setNode(vEdgeNode[3],3);
    
    pElem->setProgElem(vProgElem[0], 0);//CommElemのために頂点番号(vVertNode)順に親ElemにProgElemをセット
    
    // 要素 1
    vProgElem[1]->setNode(vEdgeNode[0],0); vProgElem[1]->setNode(vVertNode[1],1);
    vProgElem[1]->setNode(vEdgeNode[1],2); vProgElem[1]->setNode(vFaceNode[0],3);
    
    pElem->setProgElem(vProgElem[1], 1);
    
    // 要素 2
    vProgElem[2]->setNode(vEdgeNode[1],0); vProgElem[2]->setNode(vVertNode[2],1);
    vProgElem[2]->setNode(vEdgeNode[2],2); vProgElem[2]->setNode(vFaceNode[0],3);
    
    pElem->setProgElem(vProgElem[2], 2);
    
    // 要素 3
    vProgElem[3]->setNode(vEdgeNode[2],0); vProgElem[3]->setNode(vVertNode[3],1);
    vProgElem[3]->setNode(vEdgeNode[3],2); vProgElem[3]->setNode(vFaceNode[0],3);
    
    pElem->setProgElem(vProgElem[3], 3);

    // IDのセット
    for(i=0; i< 4; i++){
        //vProgElem[i]->setParentID(pElem->getID());//親の要素IDをParentIDにセット

        vProgElem[i]->setID(indexCount);//配列Indexは直前の配列数
        ++indexCount;

        pProgMesh->setElement(vProgElem[i]);
    };

    // MPC面の属性セット
    uiint iprog;
    //マスター面
    if(pElem->isMPCMaster()){
        for(iprog=0; iprog< 4; iprog++){ vProgElem[iprog]->markingMPCMaster(); vProgElem[iprog]->markingMPCFace(0);}
    }
    //スレーブ面
    if(pElem->isMPCSlave()){
        for(iprog=0; iprog< 4; iprog++){ vProgElem[iprog]->markingMPCSlave(); vProgElem[iprog]->markingMPCFace(0);}
    }
    
    //CommMesh2(節点通信界面):Quadなので通信"辺"
    uiint iedge;
    if(pElem->isCommMesh2()){
        for(iedge=0; iedge< 4; iedge++){
            if(pElem->isCommEntity(iedge)){
                switch(iedge){
                    case(0):
                        //辺番号"0" にくっついているprogElemは,progElem[0]とprogElem[1]
                        vProgElem[0]->markingCommMesh2(); vProgElem[0]->markingCommEntity(0);//prog[0]の辺=0
                        vProgElem[1]->markingCommMesh2(); vProgElem[1]->markingCommEntity(0);//prog[1]の辺=0
                        break;
                    case(1):
                        //辺番号"1" にくっついているproElemは,progElem[1]とprogElem[2]
                        vProgElem[1]->markingCommMesh2(); vProgElem[1]->markingCommEntity(1);//prog[1]の辺=1
                        vProgElem[2]->markingCommMesh2(); vProgElem[2]->markingCommEntity(0);//prog[2]の辺=0
                        break;
                    case(2):
                        //辺番号"2" にくっついているproElemは,progElem[2]とprogElem[3]
                        vProgElem[2]->markingCommMesh2(); vProgElem[2]->markingCommEntity(1);//prog[2]の辺=1
                        vProgElem[3]->markingCommMesh2(); vProgElem[3]->markingCommEntity(0);//prog[3]の辺=0
                        break;
                    case(3):
                        //辺番号"3" にくっついているproElemは,progElem[3]とprogElem[0]
                        vProgElem[3]->markingCommMesh2(); vProgElem[3]->markingCommEntity(1);//prog[3]の辺=1
                        vProgElem[0]->markingCommMesh2(); vProgElem[0]->markingCommEntity(3);//prog[0]の辺=3
                        break;
                }
            }//if( CommEntity )
        };//for(iedge)
    }//if(CommMesh2)
    
}
// 三角形の分割
//
void CMeshFactory::dividTriangle(CElement* pElem, vector<CElement*>& vProgElem, uiint& indexCount, CMesh* pProgMesh)
{
    vector<CNode*> vVertNode;//頂点のノード
    vector<CNode*> vEdgeNode;//辺のノード
    vector<CNode*> vFaceNode;//面のノード
    
    uiint numOfVert,numOfEdge,numOfFace;//各要素の属性(分割の際に使用)
    numOfVert= NumberOfVertex::Triangle(); numOfFace= NumberOfFace::Triangle(); numOfEdge= NumberOfEdge::Triangle();

    uiint i;
    //頂点のノード
    vVertNode.resize(numOfVert); for(i=0; i< numOfVert; i++){ vVertNode[i] = pElem->getNode(i);}
    //辺のノード
    vEdgeNode.resize(numOfEdge);for(i=0; i< numOfEdge; i++){ vEdgeNode[i] = pElem->getEdgeInterNode(i);}
    //面のノード
    vFaceNode.resize(numOfFace); for(i=0; i< numOfFace; i++){ vFaceNode[i] = pElem->getFaceNode(i);}
    
    // 要素 0
    vProgElem[0]->setNode(vEdgeNode[0],0); vProgElem[0]->setNode(vVertNode[1],1);
    vProgElem[0]->setNode(vEdgeNode[1],2); vProgElem[0]->setNode(vFaceNode[0],3);
    
    pElem->setProgElem(vProgElem[0], 1);//CommElemのために頂点番号(vVertNode)順に親ElemにProgElemをセット
    
    // 要素 1
    vProgElem[1]->setNode(vEdgeNode[1],0); vProgElem[1]->setNode(vVertNode[2],1);
    vProgElem[1]->setNode(vEdgeNode[2],2); vProgElem[1]->setNode(vFaceNode[0],3);
    
    pElem->setProgElem(vProgElem[1], 2);
    
    // 要素 2
    vProgElem[2]->setNode(vEdgeNode[2],0); vProgElem[2]->setNode(vVertNode[0],1);
    vProgElem[2]->setNode(vEdgeNode[0],2); vProgElem[2]->setNode(vFaceNode[0],3);
    
    pElem->setProgElem(vProgElem[2], 0);

    // IDのセット
    for(i=0; i< 3; i++){
        //vProgElem[i]->setParentID(pElem->getID());//親の要素IDをParentIDにセット

        vProgElem[i]->setID(indexCount);//配列Indexは直前の配列数
        ++indexCount;

        pProgMesh->setElement(vProgElem[i]);
    };

    // MPC面の属性セット
    uiint iprog;
    // マスター面
    if(pElem->isMPCMaster()){
        for(iprog=0; iprog< 3; iprog++){ vProgElem[iprog]->markingMPCMaster(); vProgElem[iprog]->markingMPCFace(0);}
    }
    // スレーブ面
    if(pElem->isMPCSlave()){
        for(iprog=0; iprog< 3; iprog++){ vProgElem[iprog]->markingMPCSlave(); vProgElem[iprog]->markingMPCFace(0);}
    }

    // CommMesh2(通信節点界面) : Triなので通信"辺"
    uiint iedge;
    if(pElem->isCommMesh2()){
        for(iedge=0; iedge< 3; iedge++){
            if(pElem->isCommEntity(iedge)){
                switch(iedge){
                    case(0):
                        //辺"0"に生成されるprogElemは,prog[2]とprog[0]
                        vProgElem[2]->markingCommMesh2(); vProgElem[2]->markingCommEntity(1);//progElemの辺”1”
                        vProgElem[0]->markingCommMesh2(); vProgElem[0]->markingCommEntity(0);//progElemの辺”0”
                        break;
                    case(1):
                        //辺"1"に生成されるprogElemは,prog[0]とprog[1]
                        vProgElem[0]->markingCommMesh2(); vProgElem[0]->markingCommEntity(1);//progElemの辺”1”
                        vProgElem[1]->markingCommMesh2(); vProgElem[1]->markingCommEntity(0);//progElemの辺”0”
                        break;
                    case(2):
                        //辺"2"に生成されるprogElemは,prog[1]とprog[2]
                        vProgElem[1]->markingCommMesh2(); vProgElem[1]->markingCommEntity(1);//progElemの辺”1”
                        vProgElem[2]->markingCommMesh2(); vProgElem[2]->markingCommEntity(0);//progElemの辺”0”
                        break;
                }
            }// if( CommEntity )
        };
    }// if(CommMesh2)
}
// ビームの分割
//
void CMeshFactory::dividBeam(CElement* pElem, vector<CElement*>& vProgElem, uiint& indexCount, CMesh* pProgMesh)
{
    vector<CNode*> vVertNode;//頂点のノード
    vector<CNode*> vEdgeNode;//辺のノード
    
    uiint numOfVert,numOfEdge;//各要素の属性(分割の際に使用)
    numOfVert= NumberOfVertex::Beam(); numOfEdge= NumberOfEdge::Beam();

    uiint i;
    //頂点のノード
    vVertNode.resize(numOfVert); for(i=0; i< numOfVert; i++){ vVertNode[i] = pElem->getNode(i);}
    //辺のノード
    vEdgeNode.resize(numOfEdge); for(i=0; i< numOfEdge; i++){ vEdgeNode[i] = pElem->getEdgeInterNode(i);}
    
    // 要素 0
    vProgElem[0]->setNode(vVertNode[0],0); vProgElem[0]->setNode(vEdgeNode[0],1);
    
    pElem->setProgElem(vProgElem[0], 0);//CommElemのために頂点番号(vVertNode)順に親ElemにProgElemをセット
    
    // 要素 1
    vProgElem[1]->setNode(vEdgeNode[0],0); vProgElem[1]->setNode(vVertNode[1],1);

    pElem->setProgElem(vProgElem[1], 1);//CommElemのために頂点番号(vVertNode)順に親ElemにProgElemをセット

    // IDのセット
    for(i=0; i< 2; i++){
        //vProgElem[i]->setParentID(pElem->getID());//親の要素IDをParentIDにセット

        vProgElem[i]->setID(indexCount);//配列Indexは直前の配列数
        ++indexCount;

        pProgMesh->setElement(vProgElem[i]);
    };

    // MPC面の属性セット
    uiint iprog;
    // マスター
    if(pElem->isMPCMaster()){
        for(iprog=0; iprog< 2; iprog++) vProgElem[iprog]->markingMPCMaster();
    }
    // スレーブ
    if(pElem->isMPCSlave()){
        for(iprog=0; iprog< 2; iprog++) vProgElem[iprog]->markingMPCSlave();
    }

    // CommMesh2(節点通信界面) :Beamなので通信”点” { progしても変化なし.}
    uiint ivert;
    if(pElem->isCommMesh2()){
        for(ivert=0; ivert< 2; ivert++){
            if(pElem->isCommEntity(ivert)){
                switch(ivert){
                    case(0):
                        //頂点"0"に生成されるprogElem, progElemの頂点"0"が通信点
                        vProgElem[0]->markingCommMesh2(); vProgElem[0]->markingCommEntity(0);
                        break;
                    case(1):
                        //頂点"1"に生成されるprogElem, progElemの頂点"1"が通信点
                        vProgElem[1]->markingCommMesh2(); vProgElem[1]->markingCommEntity(1);
                        break;
                }
            }//if(CommEntity)
        };
    }// if(CommMesh2)
}



// setup to BucketMesh in AssyModel
//
void CMeshFactory::setupBucketMesh(const uiint& mgLevel, const uiint& maxID, const uiint& minID)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);

    mpTAssyModel->intializeBucket(maxID, minID);

    mpTAssyModel->setMaxMeshID(maxID);
    mpTAssyModel->setMinMeshID(minID);
}

//
// GMGModel MultiGridの各層のAssyModelを全て生成(AssyModel変数を確保)
//
void CMeshFactory::GeneAssyModel(const uiint& nNumOfLevel)
{
    //--------  2011.04.22 全面変更 --------//

    if(nNumOfLevel < 2) return;//初期化時に1個生成済み

    //    mpGMGModel->initAssyModel(nNumOfLevel);
    //    mpGMGModel->reserveAssyModel(nNumOfLevel);

    mpGMGModel->resizeAssyModel(nNumOfLevel);
    
    for(uiint i=1; i< nNumOfLevel; i++){
        mpTAssyModel = new CAssyModel();
        mpTAssyModel->setMGLevel(i);

        mpGMGModel->addModel(mpTAssyModel,i);
    };
}

// Mesh リザーブ
//
void CMeshFactory::reserveMesh(const uiint& mgLevel, const uiint& num_of_mesh)
{
    mpTAssyModel= mpGMGModel->getAssyModel(mgLevel);
    mpTAssyModel->resizeMesh(num_of_mesh);
}

// Mesh set to AssyModel
//
void CMeshFactory::GeneMesh(const uiint& mgLevel, const uiint& mesh_id, const uiint& index, const uiint& nProp)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);

    //AssyModelにMeshを生成してセット
    //----
    mpTMesh = new CMesh();

    mpTMesh->setMeshID(mesh_id);
    mpTMesh->setMGLevel(mgLevel);
    mpTMesh->setMaxMGLevel(mMGLevel);//Factoryのレベル==最大階層数
    mpTMesh->setSolutionType(mnSolutionType);
    mpTMesh->setProp(nProp);

    mpTAssyModel->setBucket(mesh_id, index);
    mpTAssyModel->setMesh(mpTMesh, index);

    //各MeshにBNodeMeshGrpを生成してセット
    //----
    CBNodeMeshGrp *pBNodeMeshGrp= new CBNodeMeshGrp;
    mpTMesh->setBNodeMeshGrp(pBNodeMeshGrp);
}


// Node Genetate 
//
void CMeshFactory::GeneNode(const uiint& mgLevel, const uiint& mesh_id, const uiint& id, const vdouble& coord,
                            const uiint& nodeType, const uiint& nNumOfSDOF, const uiint& nNumOfVDOF)
{
    CNode *pNode;

    switch(nodeType){
        case(NodeType::Scalar):
            pNode = new CScalarNode();
            //pNode->resizeScalar(numOfScaParam);
            pNode->setScalarDOF(nNumOfSDOF);
            break;
        case(NodeType::Vector):
            pNode = new CVectorNode();
            //pNode->resizeVector(numOfVecParam);
            pNode->setVectorDOF(nNumOfVDOF);
            break;
        case(NodeType::ScalarVector):
            pNode = new CScalarVectorNode();
            //pNode->resizeScalar(numOfScaParam);
            //pNode->resizeVector(numOfVecParam);
            pNode->setScalarDOF(nNumOfSDOF);
            pNode->setVectorDOF(nNumOfVDOF);
            break;
        default:
            //pNode->InitializeNodeADOF(vParam, num_of_param);
            break;
    }
    pNode->setID(id);// id は、連続したIndex番号にする予定(09.06.23)
    pNode->setCoord(coord);

    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);// MultiGrid Level==0

    if(!mpTAssyModel) mpLogger->Info(Utility::LoggerMode::MWDebug, "AssyModel => NULL");//debug

    mpTMesh = mpTAssyModel->getMesh(mesh_id);
    ////debug
    //if(!mpTMesh) cout << "Factory::mpTMesh == NULL" << endl;

    //mpTMesh->setNode(pNode,id);
    mpTMesh->setNode(pNode);
}


// Mesh::seupNumOfNode
//
void CMeshFactory::setupNode(const uiint& mgLevel, const uiint& mesh_id)
{
    mpTAssyModel= mpGMGModel->getAssyModel(mgLevel);
    mpTMesh= mpTAssyModel->getMesh(mesh_id);
    
    mpTMesh->setupNumOfNode();
}

// Mesh::reserveNode
//
void CMeshFactory::reserveNode(const uiint& mgLevel, const uiint& mesh_id, const uiint& num_of_node)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh(mesh_id);

    mpTMesh->reserveNode(num_of_node);
}



// Element Generate
//
void CMeshFactory::GeneElement(const uiint& mgLevel, const uiint& mesh_id, const uiint& id, const uiint& type_num, const vint& node_id)
{
    CElement *pElement;

    switch(type_num){
        // 1次
        case(ElementType::Hexa):
            pElement = new CHexa;
            break;
        case(ElementType::Tetra):
            pElement = new CTetra;
            break;
        case(ElementType::Prism):
            pElement = new CPrism;
            break;
        case(ElementType::Quad):
            pElement = new CQuad;
            break;
        case(ElementType::Triangle):
            pElement = new CTriangle;
            break;
        case(ElementType::Beam):
            pElement = new CBeam;
            break;
        // 2次
        case(ElementType::Hexa2):
            pElement = new CHexa2;
            break;
        case(ElementType::Tetra2):
            pElement = new CTetra2;
            break;
        case(ElementType::Prism2):
            pElement = new CPrism2;
            break;
        case(ElementType::Quad2):
            pElement = new CQuad2;
            break;
        case(ElementType::Triangle2):
            pElement = new CTriangle2;
            break;
        case(ElementType::Beam2):
            pElement = new CBeam2;
            break;
        default:
            mpLogger->Info(Utility::LoggerMode::Error,"Error::GeneElement at Factory");
            break;
    }
    pElement->initialize();
    pElement->setID(id);

    CNode *pNode;
    uiint i;
    // Node* setup
    //
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);    
    mpTMesh = mpTAssyModel->getMesh(mesh_id);
    for(i=0; i < node_id.size(); i++){
        pNode = mpTMesh->getNode(node_id[i]);
        pElement->setNode(pNode, i);
    };
    //2次要素の場合"EdgeInterNode"にセット
    if(pElement->getOrder()==ElementOrder::Second){
        uiint iedge;
        uiint nNumOfVert = pElement->getNumOfVert();
        for(iedge=0; iedge < pElement->getNumOfEdge(); iedge++){

            uiint nNodeID = node_id[nNumOfVert + iedge];

            pNode = mpTMesh->getNode(nNodeID);
            pElement->setEdgeInterNode(pNode, iedge);
        };
    }
    
    // Elem to Mesh
    //mpTMesh->setElement(pElement,id);
    mpTMesh->setElement(pElement);
}


// Mesh::setupNumOfElement
//
void CMeshFactory::setupElement(const uiint& mgLevel, const uiint& mesh_id)
{
    mpTAssyModel= mpGMGModel->getAssyModel(mgLevel);
    mpTMesh= mpTAssyModel->getMesh(mesh_id);
    mpTMesh->setupNumOfElement();
}

// Mesh::reserveElement
//
void CMeshFactory::reserveElement(const uiint& mgLevel, const uiint& mesh_id, const uiint& num_of_element)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh(mesh_id);
    mpTMesh->reserveElement(num_of_element);
}

// 節点まわりの要素集合
// Mesh::reserveAggElement
//
void CMeshFactory::resizeAggregate(const uiint& mgLevel, const uiint& mesh_id, const uiint& num_of_node)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh(mesh_id);

    mpTMesh->resizeAggregate(num_of_node);
}

void CMeshFactory::GeneAggregate(const uiint& mgLevel, const uiint& mesh_id, const uiint& num_of_node)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh(mesh_id);

    uiint i;
    if(mnSolutionType==SolutionType::FEM){
        for(i=0; i< num_of_node; i++){
            CAggregateElement *pAggElem = new CAggregateElement;

            mpTMesh->setAggElement(pAggElem, i);//Node周辺の要素集合
        };
    }
    if(mnSolutionType==SolutionType::FVM){
        for(i=0; i< num_of_node; i++){
            CAggregateNode    *pAggNode = new CAggregateNode;

            mpTMesh->setAggNode(pAggNode, i);//Node周辺のNode集合
        };
    }
}


//----
// Boundary
//----
void CMeshFactory::reserveBoundaryNodeMesh(const uiint& mgLevel, const uiint& mesh_id, const uiint& num_of_bnd)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh_ID(mesh_id);

    //    cout << "MeshFactory::reserveBoundaryNodeMesh, NumOfBnd " << num_of_bnd << endl;

    mpTMesh->reserveBndNodeMesh(num_of_bnd);
}
// BoundaryNodeMesh 生成
void CMeshFactory::GeneBoundaryNodeMesh(const uiint& mgLevel, const uiint& mesh_id, const uiint& bnd_id, const uiint& bnd_type, const string& bnd_name)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh_ID(mesh_id);
    
    CBoundaryNodeMesh *pBNodeMesh= new CBoundaryNodeMesh;

    pBNodeMesh->setID(bnd_id);
    pBNodeMesh->setBndType(bnd_type);
    pBNodeMesh->setName(bnd_name);

    mpTMesh->setBndNodeMesh(pBNodeMesh);
}
uiint CMeshFactory::getNumOfBounaryNodeMesh(const uiint& mgLevel, const uiint& mesh_id)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh_ID(mesh_id);

    return mpTMesh->getNumOfBoundaryNodeMesh();
}

void CMeshFactory::reserveBoundaryFaceMesh(const uiint& mgLevel, const uiint& mesh_id, const uiint& num_of_bnd)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh_ID(mesh_id);
    
    mpTMesh->reserveBndFaceMesh(num_of_bnd);
}
// BoundaryFaceMesh 生成
void CMeshFactory::GeneBoundaryFaceMesh(const uiint& mgLevel, const uiint& mesh_id, const uiint& bnd_id, const uiint& bnd_type, const string& bnd_name,
                                        const uiint& numOfDOF, const vuint& vDOF)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh_ID(mesh_id);
    

    CBoundaryFaceMesh *pBFaceMesh= new CBoundaryFaceMesh;

    pBFaceMesh->setMGLevel(mgLevel);
    // pBFaceMesh->setMaxMGLevel(mMGLevel);// CMW::Refineに移行(CGrid処理)
    pBFaceMesh->setID(bnd_id);
    pBFaceMesh->setBndType(bnd_type);
    pBFaceMesh->setName(bnd_name);

    pBFaceMesh->resizeDOF(numOfDOF);

    if(numOfDOF != vDOF.size()) mpLogger->Info(Utility::LoggerMode::Error, "CMeshFactory::GeneBoundaryFaceMesh, invalid argument");

    uiint idof, dof;
    for(idof=0; idof < vDOF.size(); idof++){
        dof = vDOF[idof];
        pBFaceMesh->setDOF(idof, dof);
    };

    mpTMesh->setBndFaceMesh(pBFaceMesh);

}
uiint CMeshFactory::getNumOfBounaryFaceMesh(const uiint& mgLevel, const uiint& mesh_id)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh_ID(mesh_id);

    return mpTMesh->getNumOfBoundaryFaceMesh();
}
CBoundaryFaceMesh* CMeshFactory::getBoundaryFaceMesh(const uiint& mgLevel, const uiint& mesh_id, const uiint& bnd_id)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh_ID(mesh_id);

    return mpTMesh->getBndFaceMeshID(bnd_id);
}

void CMeshFactory::reserveBoundaryVolumeMesh(const uiint& mgLevel, const uiint& mesh_id, const uiint& num_of_bnd)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh_ID(mesh_id);

    mpTMesh->reserveBndVolumeMesh(num_of_bnd);
}
// BoundaryVolumeMesh 生成
void CMeshFactory::GeneBoundaryVolumeMesh(const uiint& mgLevel, const uiint& mesh_id, const uiint& bnd_id, const uiint& bnd_type, const string& bnd_name,
                                          const uiint& numOfDOF, const vuint& vDOF)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh_ID(mesh_id);


    CBoundaryVolumeMesh *pBVolMesh= new CBoundaryVolumeMesh;
    
    pBVolMesh->setMGLevel(mgLevel);
    // pBVolMesh->setMaxMGLevel(mMGLevel);// CMW::Refineに移行(CGrid処理)
    pBVolMesh->setID(bnd_id);
    pBVolMesh->setBndType(bnd_type);
    pBVolMesh->setName(bnd_name);

    pBVolMesh->resizeDOF(numOfDOF);

    if(numOfDOF != vDOF.size()) mpLogger->Info(Utility::LoggerMode::Error, "CMeshFactory::GeneBoundaryVolumeMesh, invalid argument");

    uiint idof, dof;
    for(idof=0; idof < vDOF.size(); idof++){
        dof = vDOF[idof];
        pBVolMesh->setDOF(idof, dof);
    }

    mpTMesh->setBndVolumeMesh(pBVolMesh);
    
}
uiint CMeshFactory::getNumOfBounaryVolumeMesh(const uiint& mgLevel, const uiint& mesh_id)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh_ID(mesh_id);
    
    return mpTMesh->getNumOfBoundaryVolumeMesh();
}
CBoundaryVolumeMesh* CMeshFactory::getBoundaryVolumeMesh(const uiint& mgLevel, const uiint& mesh_id, const uiint& bnd_id)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh_ID(mesh_id);

    return mpTMesh->getBndVolumeMeshID(bnd_id);
}

void CMeshFactory::reserveBoundaryEdgeMesh(const uiint& mgLevel, const uiint& mesh_id, const uiint& num_of_bnd)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh_ID(mesh_id);

    mpTMesh->reserveBndEdgeMesh(num_of_bnd);
}
// BoundaryEdgeMesh 生成
void CMeshFactory::GeneBoundaryEdgeMesh(const uiint& mgLevel, const uiint& mesh_id, const uiint& bnd_id, const uiint& bnd_type, const string& bnd_name,
                   const uiint& numOfDOF, const vuint& vDOF)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh_ID(mesh_id);

    CBoundaryEdgeMesh *pBEdgeMesh= new CBoundaryEdgeMesh;
    
    pBEdgeMesh->setMGLevel(mgLevel);
    // pBEdgeMesh->setMaxMGLevel(mMGLevel);// CMW::Refineに移行(CGrid処理)
    pBEdgeMesh->setID(bnd_id);
    pBEdgeMesh->setBndType(bnd_type);
    pBEdgeMesh->setName(bnd_name);

    pBEdgeMesh->resizeDOF(numOfDOF);
    if(numOfDOF != vDOF.size()) mpLogger->Info(Utility::LoggerMode::Error, "CMeshFactory::GeneBoundaryEdgeMesh, invalid argument");

    uiint idof, dof;
    for(idof=0; idof < numOfDOF; idof++){
        dof = vDOF[idof];
        pBEdgeMesh->setDOF(idof, dof);
    }

    mpTMesh->setBndEdgeMesh(pBEdgeMesh);
}

uiint CMeshFactory::getNumOfBounaryEdgeMesh(const uiint& mgLevel, const uiint& mesh_id)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh_ID(mesh_id);

    return mpTMesh->getNumOfBoundaryEdgeMesh();
}
CBoundaryEdgeMesh* CMeshFactory::getBoundaryEdgeMesh(const uiint& mgLevel, const uiint& mesh_id, const uiint& bnd_id)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh_ID(mesh_id);

    return mpTMesh->getBndEdgeMeshID(bnd_id);
}
// "BoundaryNodeMesh"の節点(BoundarySBNode)生成
// ----
void CMeshFactory::GeneBoundaryNode(const uiint& mgLevel, const uiint& bnd_id, const uiint& bndType,
                          const uiint& mesh_id, const uiint& node_id,
                          const uiint& b_node_id, const uiint& dof, const double& val)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh_ID(mesh_id);

    CBoundaryNodeMesh* pBndNodeMesh= mpTMesh->getBndNodeMeshID(bnd_id);
    pBndNodeMesh->setBndType(bndType);

    //    cout << "MeshFactory::GeneBoundaryNode, BndNodeMesh id " << pBndNodeMesh->getID() << ", bnd_id " << bnd_id
    //         << ", BNode " << b_node_id << ", Node " << node_id << ", dof " << dof << endl;
    
    // *DOF違いの同一BNodeがセットされるので,BNodeIDによる選別.
    // ----
    // BoundaryMeshに既に存在するか判定
    // ----
    uiint crIndex= pBndNodeMesh->getNumOfBNode();
    uiint crBNodeID;
    CBoundarySBNode *pCrBNode;
    uiint ib; bool bfind(false);

    if(crIndex > 0){
        if(crIndex==1){
            pCrBNode= pBndNodeMesh->getBNodeIX(0);
            crBNodeID= pCrBNode->getID();

            if(b_node_id==crBNodeID) bfind= true;
            //cout << "crBNodeID= " << crBNodeID << ", b_node_id= " << b_node_id << endl;
        }
        for(ib=crIndex-1; ib > 0; ib--){
            pCrBNode= pBndNodeMesh->getBNodeIX(ib);
            crBNodeID= pCrBNode->getID();
            
            if(b_node_id==crBNodeID) bfind= true;
        };
    }
    
    
    if(bfind){
        // 既に生成済みのBNodeの場合
        pCrBNode= pBndNodeMesh->getBNodeID(b_node_id);
        pCrBNode->addDOF(dof);
        pCrBNode->setValue(dof, val);
    }else{
        // 新たにBNodeを生成する場合
        CNode *pNode= mpTMesh->getNode(node_id);
        
        CBoundarySBNode *pBNode = new CBoundarySBNode();

        pBNode->setNode(pNode);
        pBNode->setID(b_node_id);
        

        pBNode->addDOF(dof);
        pBNode->setValue(dof, val);
        pBndNodeMesh->addBNode(pBNode);
    }
}

// BoundaryFaceMeshの節点(BoundaryNode)生成
// ----
void CMeshFactory::GeneBoundaryFaceNode(const uiint& mgLevel, const uiint& bnd_id, const uiint& bndType,
        const uiint& mesh_id, const uiint& node_id, const uiint& b_node_id)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh_ID(mesh_id);

    // 境界面メッシュ
    CBoundaryFaceMesh *pBFaceMesh= mpTMesh->getBndFaceMeshID(bnd_id);
    // ----------------------------------------------
    // 境界面メッシュへの属性設定は,GeneBoundaryFaceで実行
    // ----------------------------------------------


    CNode *pNode= mpTMesh->getNode(node_id);
    // 境界面メッシュのBNode
    CBoundaryNode *pBNode= new CBoundaryNode;// <<<<<<<<<<<<<< new
    pBNode->setNode(pNode);
    pBNode->setID(b_node_id);
    pBNode->setMGLevel(mgLevel);
    
    // -> CMW::Refineで再度領域確保(CGrid処理) mMGLevelは初期段階で"0" 
    // -> ファイル入力値をセットするため領域を”1"確保
    pBNode->resizeValue(mMGLevel+1);

    pBFaceMesh->addBNode(pBNode);//面メッシュへBNodeを追加
}
// コースグリッドでBNodeに境界値が存在するとする:新Dirichlet用途
void CMeshFactory::setValue_BoundaryFaceNode(const uiint& mesh_id, const uiint& bnd_id, const uiint& bnode_id, const uiint& dof, const double& val)
{
    mpTAssyModel = mpGMGModel->getAssyModel(0);//コースグリッド
    mpTMesh = mpTAssyModel->getMesh_ID(mesh_id);

    CBoundaryFaceMesh *pBFaceMesh= mpTMesh->getBndFaceMeshID(bnd_id);
    CBoundaryNode *pBNode= pBFaceMesh->getBNodeID(bnode_id);

    pBNode->addValue(dof, 0, val);
}
// BNode周辺のAggregate情報の初期化
//
void CMeshFactory::initFaceAggregate(const uiint& mgLevel, const uiint& mesh_id, const uiint& bnd_id)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh_ID(mesh_id);

    // 境界面メッシュ
    CBoundaryFaceMesh *pBFaceMesh= mpTMesh->getBndFaceMeshID(bnd_id);

    //pBFaceMesh->resizeAggFace();
    pBFaceMesh->setupAggFace();
}
void CMeshFactory::resizeFaceAggregate(const uiint& mgLevel, const uiint& mesh_id, const uiint& bnd_id)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh_ID(mesh_id);

    // 境界面メッシュ
    CBoundaryFaceMesh *pBFaceMesh= mpTMesh->getBndFaceMeshID(bnd_id);

    pBFaceMesh->resizeAggFace();
}
// BoundaryFaceMeshの面(BoundaryFace) 生成
// ----
void CMeshFactory::GeneBoundaryFace(const uiint& mgLevel, const uiint& bnd_id, const uiint& bndType, const uiint& elemType,
                            const uiint& mesh_id, const uiint& elem_id, const uiint& face_id, vuint& vBNodeID,
                            const uiint& b_face_id, const uiint& dof, const double& val)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh(mesh_id);

    // 境界面メッシュ
    CBoundaryFaceMesh *pBFaceMesh= mpTMesh->getBndFaceMeshID(bnd_id);

    pBFaceMesh->setBndType(bndType);
    pBFaceMesh->setID(bnd_id);

    //cout << "MeshFactory::GeneBoundaryFace A, pBFaceMesh_ID = " << pBFaceMesh->getID() << endl;

    // 境界面
    // *DOF違いの同一BFaceがセットされるので,BFaceIDによる選別.
    // ----
    // BoundaryMeshに既に存在するか判定
    // ----
    uiint crIndex= pBFaceMesh->getNumOfBFace();
    uiint crBFaceID;
    CBoundaryFace *pCrBFace;
    uiint ib; bool bfind(false);
    if(crIndex > 0){
        if(crIndex==1){
            pCrBFace= pBFaceMesh->getBFaceIX(0);
            crBFaceID= pCrBFace->getID();

            if(b_face_id==crBFaceID) bfind= true;
        }
        for(ib=crIndex-1; ib > 0; ib--){
            pCrBFace= pBFaceMesh->getBFaceIX(ib);
            crBFaceID= pCrBFace->getID();

            if(b_face_id==crBFaceID) bfind= true;
        };
    }

    CBoundaryFace *pBFace;

    if(bfind){
        // 既存のFaceMeshにBFaceが存在する場合
        pBFace= pBFaceMesh->getBFaceID(b_face_id);

        if(bndType==BoundaryType::Neumann){
            double dArea= pBFace->getArea();
            double dBndValue= val * dArea;//分布値*面積
            pBFace->setBndValue(dof, dBndValue);
        }
        //cout << "pBFace id= " << pBFace->getID() << endl;

    }else{
        // 新規にBFaceを生成する場合
        pBFace = new CBoundaryFace();// <<<<<<<<<<<<<< new

        pBFace->setElementID(elem_id);
        pBFace->setElementFaceID(face_id);
        pBFace->setID(b_face_id);
        pBFace->setBFaceShape(elemType);
        //pBFace->addDOF(dof);
        //pBFace->setBndValue(dof,val);//->分布値*面積に変更のため、面積計算後に移動

        CElement *pElem= mpTMesh->getElement(elem_id);
        pBFace->setElement(pElem);

        //cout << "pBFace id= " << pBFace->getID() << endl;

        //uint iedge;
        switch(elemType){
            case(ElementType::Quad):case(ElementType::Quad2):
                pBFace->resizeBNode(4);
                break;
            //case(ElementType::Quad2):
            //    pBFace->resizeBNode(8);
            //    for(iedge=0; iedge < 4; iedge++) pBFace->markingEdge(iedge);// 辺Nodeマーキング
            //    break;
            case(ElementType::Triangle):case(ElementType::Triangle2):
                pBFace->resizeBNode(3);
                break;
            //case(ElementType::Triangle2):
            //    pBFace->resizeBNode(6);
            //    for(iedge=0; iedge < 3; iedge++) pBFace->markingEdge(iedge);// 辺Nodeマーキング
            //    break;
            default:
                //TODO:Logger
                break;
        }

        pBFaceMesh->addBFace(pBFace);
    }

    //新規にBFaceを生成した場合のみBNodeをセット
    if(!bfind){
        // 境界面節点
        CBoundaryNode *pBNode;
        uiint numOfVert= pBFace->getNumOfVert();
        uiint ivert;
        for(ivert=0; ivert < numOfVert; ivert++){
            pBNode= pBFaceMesh->getBNodeID(vBNodeID[ivert]);
            pBFace->setBNode(ivert, pBNode);
        };
        uiint iedge;
        //2次の場合-辺BNodeをセット
        if(pBFace->getOrder()==ElementOrder::Second){

            uiint nNumOfVert = pBFace->getNumOfVert();
            uiint nNumOfEdge = pBFace->getNumOfEdge();
            uiint id;
            for(iedge=0; iedge < nNumOfEdge; iedge++){
                id = vBNodeID[nNumOfVert + iedge];
                pBNode= pBFaceMesh->getBNodeID(id);

                // 辺BNodeに対応する、AggFaceをセット
                //
                uiint ibnode = pBFaceMesh->getBNodeIndex(id);
                pBFaceMesh->setAggFace(ibnode, pBFace->getID());

                pBFace->setEdgeBNode(iedge, pBNode);// 辺BNodeセット
                pBFace->markingEdge(iedge);         // 辺マーキング
            };

            pBFace->replaceEdgeBNode();//mvBNodeへの移設
        }
        pBFace->calcArea();
        
        if(bndType==BoundaryType::Neumann){
            double dArea= pBFace->getArea();
            double dBndValue= val * dArea;//分布値*面積
            pBFace->setBndValue(dof, dBndValue);
        }
    }//if (!bfind) end
}

// BoundaryVolumeMeshの節点(BoundaryNode)生成
// ----
void CMeshFactory::GeneBoundaryVolumeNode(const uiint& mgLevel, const uiint& bnd_id, const uiint& bndType,
        const uiint& mesh_id, const uiint& node_id, const uiint& b_node_id)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh_ID(mesh_id);

    // 境界体積メッシュ
    CBoundaryVolumeMesh *pBVolumeMesh= mpTMesh->getBndVolumeMeshID(bnd_id);
    // ----------------------------------------------
    // 境界体積メッシュへの属性設定は,GeneBoundaryVolumeで実行
    // ----------------------------------------------

    CNode *pNode= mpTMesh->getNode(node_id);
    // 境界面メッシュのBNode
    CBoundaryNode *pBNode= new CBoundaryNode;// <<<<<<<<<<<<< new
    pBNode->setNode(pNode);
    pBNode->setID(b_node_id);
    pBNode->setMGLevel(mgLevel);
    
    // CMW::Refineで再度領域確保(CGrid処理) mMGLevelは初期段階で"0" 
    // -> ファイル入力値をセットするため領域を”1"確保
    pBNode->resizeValue(mMGLevel+1);


    pBVolumeMesh->addBNode(pBNode);//体積メッシュへBNodeを追加
}
// コースグリッドでBNodeに境界値が存在するとする:新Dirichlet用途
void CMeshFactory::setValue_BoundaryVolumeNode(const uiint& mesh_id, const uiint& bnd_id, const uiint& bnode_id, const uiint& dof, const double& val)
{
    mpTAssyModel = mpGMGModel->getAssyModel(0);//コースグリッド
    mpTMesh = mpTAssyModel->getMesh_ID(mesh_id);

    CBoundaryVolumeMesh *pBVolMesh= mpTMesh->getBndVolumeMeshID(bnd_id);
    CBoundaryNode *pBNode= pBVolMesh->getBNodeID(bnode_id);

    pBNode->addValue(dof, 0, val);
}
// BNode周囲のAggregateVolume情報の初期化
//
void CMeshFactory::initVolumeAggregate(const uiint& mgLevel, const uiint& mesh_id, const uiint& bnd_id)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh_ID(mesh_id);

    // 境界体積メッシュ
    CBoundaryVolumeMesh *pBVolumeMesh= mpTMesh->getBndVolumeMeshID(bnd_id);

    //pBVolumeMesh->resizeAggVol();
    pBVolumeMesh->setupAggVol();
}
void CMeshFactory::resizeVolumeAggregate(const uiint& mgLevel, const uiint& mesh_id, const uiint& bnd_id)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh_ID(mesh_id);

    // 境界体積メッシュ
    CBoundaryVolumeMesh *pBVolumeMesh= mpTMesh->getBndVolumeMeshID(bnd_id);

    pBVolumeMesh->resizeAggVol();
}
// BoundaryVolumeMeshの体積(BoundaryVolume) 生成
// ----
void CMeshFactory::GeneBoundaryVolume(const uiint& mgLevel, const uiint& bnd_id, const uiint& bndType, const uiint& elemType,
                            const uiint& mesh_id, const uiint& elem_id, vuint& vBNodeID,
                            const uiint& b_vol_id, const uiint& dof, const double& val)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh(mesh_id);

    // 境界体積メッシュ
    CBoundaryVolumeMesh *pBVolumeMesh= mpTMesh->getBndVolumeMeshID(bnd_id);
    pBVolumeMesh->setBndType(bndType);
    pBVolumeMesh->setID(bnd_id);

    // 境界体積
    // *DOF違いの同一BVolumeがセットされるので,BVolumeIDによる選別.
    // ----
    // BoundaryMeshに既に存在するか判定
    // ----
    uiint crIndex= pBVolumeMesh->getNumOfVolume();
    uiint crBVolumeID;
    CBoundaryVolume *pCrBVolume;
    uiint ib; bool bfind(false);
    if(crIndex > 0){
        if(crIndex==1){
            pCrBVolume= pBVolumeMesh->getBVolumeIX(0);
            crBVolumeID= pCrBVolume->getID();

            if(b_vol_id==crBVolumeID) bfind= true;
        }
        for(ib=crIndex-1; ib > 0; ib--){
            pCrBVolume= pBVolumeMesh->getBVolumeIX(ib);
            crBVolumeID= pCrBVolume->getID();

            if(b_vol_id==crBVolumeID) bfind= true;
        };
    }

    CBoundaryVolume *pBVolume;

    if(bfind){
        // 既存のVolumeMeshにBVolumeが存在する場合
        pBVolume= pBVolumeMesh->getBVolumeID(b_vol_id);
        //pBVolume->addDOF(dof);

        if(bndType==BoundaryType::Neumann){
            double dCubicVol= pBVolume->getCubicVolume();
            double dBndValue= dCubicVol*val;//体積*分布値
            pBVolume->setBndValue(dof,dBndValue);
        }

    }else{
        // 新規にBVolumeを生成する場合
        // ----
        switch(elemType){
            case(ElementType::Hexa):
                pBVolume = new CBoundaryHexa;// <<<<<<<<<<<<<< new
                pBVolume->setOrder(ElementOrder::First);
                break;
            case(ElementType::Hexa2):
                pBVolume = new CBoundaryHexa;// <<<<<<<<<<<<<< new
                pBVolume->setOrder(ElementOrder::Second);
                break;
            case(ElementType::Tetra):
                pBVolume = new CBoundaryTetra;// <<<<<<<<<<<<< new
                pBVolume->setOrder(ElementOrder::First);
                break;
            case(ElementType::Tetra2):
                pBVolume = new CBoundaryTetra;// <<<<<<<<<<<<< new
                pBVolume->setOrder(ElementOrder::Second);
                break;
            case(ElementType::Prism):
                pBVolume = new CBoundaryPrism;// <<<<<<<<<<<<< new
                pBVolume->setOrder(ElementOrder::First);
                break;
            case(ElementType::Prism2):
                pBVolume = new CBoundaryPrism;// <<<<<<<<<<<<< new
                pBVolume->setOrder(ElementOrder::Second);
                break;
            default:
                mpLogger->Info(Utility::LoggerMode::Error, "invalid ElementType, CMeshFactory::GeneBoundaryVolume");
                break;
        }

        pBVolume->setElementID(elem_id);
        pBVolume->setID(b_vol_id);
        //pBVolume->setBndValue(dof,val);// -> 体積計算後に移行:分布値を入力値とするため.

        CElement *pElem= mpTMesh->getElement(elem_id);
        pBVolume->setElement(pElem);

        pBVolumeMesh->addBVolume(pBVolume);
    }

    //新規のBVolumeを生成した場合のみBNodeをセット
    if(!bfind){
        // 境界体積節点
        CBoundaryNode *pBNode;
        uiint numOfVert= pBVolume->getNumOfVert();
        uiint ivert;
        for(ivert=0; ivert < numOfVert; ivert++){
            pBNode= pBVolumeMesh->getBNodeID(vBNodeID[ivert]);
            pBVolume->setBNode(ivert, pBNode);
        };
        //2次要素の場合、辺BNodeをセット
        if(pBVolume->getOrder()==ElementOrder::Second){
            uiint nNumOfVert = pBVolume->getNumOfVert();
            uiint nNumOfEdge = pBVolume->getNumOfEdge();
            uiint id;
            for(uiint iedge=0; iedge < nNumOfEdge; iedge++){
                id = vBNodeID[nNumOfVert + iedge];
                pBNode= pBVolumeMesh->getBNodeID(id);

                // 辺BNodeに対応する、AggVolumeをセット
                //
                uiint ibnode = pBVolumeMesh->getBNodeIndex(id);
                pBVolumeMesh->setAggVol(ibnode, pBVolume->getID());

                pBVolume->setEdgeBNode(iedge, pBNode);// 辺BNodeセット
                pBVolume->markingEdge(iedge);         // 辺マーキング

                pBVolume->replaceEdgeBNode(iedge);//mvBNodeへの移設
            };
        }
        pBVolume->calcVolume();

        if(bndType==BoundaryType::Neumann){
            double dCubicVol= pBVolume->getCubicVolume();
            double dBndValue= dCubicVol*val;//体積*分布値
            pBVolume->setBndValue(dof, dBndValue);
        }
    }//if (!bfind) end
}


// BoundaryEdgeMeshの節点(BoundaryNode)生成
// ----
void CMeshFactory::GeneBoundaryEdgeNode(const uiint& mgLevel, const uiint& bnd_id, const uiint& bndType,
        const uiint& mesh_id, const uiint& node_id, const uiint& b_node_id)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh_ID(mesh_id);

    // 境界辺メッシュ
    CBoundaryEdgeMesh *pBEdgeMesh= mpTMesh->getBndEdgeMeshID(bnd_id);
    // ----------------------------------------------
    // 境界辺メッシュへの属性設定は,GeneBoundaryEdgeで実行
    // ----------------------------------------------

    CNode *pNode= mpTMesh->getNode(node_id);
    // 境界面メッシュのBNode
    CBoundaryNode *pBNode= new CBoundaryNode;// <<<<<<<<<<<<< new


    pBNode->setNode(pNode);
    pBNode->setID(b_node_id);
    pBNode->setMGLevel(mgLevel);
    
    // CMW::Refineで再度領域確保(CGrid処理) mMGLevelは初期段階で"0" 
    // -> ファイル入力値をセットするため領域を”1"確保
    pBNode->resizeValue(mMGLevel+1);


    ////debug
    //cout << "MeshFactory::GeneBoundaryEdgeNode, BNodeID= " << pBNode->getID() << ", NodeID= " << pNode->getID() << endl;

    pBEdgeMesh->addBNode(pBNode);//辺メッシュへBNodeを追加
}
// コースグリッドでBNodeに境界値が存在するとする:新Dirichlet用途
void CMeshFactory::setValue_BoundaryEdgeNode(const uiint& mesh_id, const uiint& bnd_id, const uiint& bnode_id, const uiint& dof, const double& val)
{
    mpTAssyModel = mpGMGModel->getAssyModel(0);//コースグリッド
    mpTMesh = mpTAssyModel->getMesh_ID(mesh_id);

    CBoundaryEdgeMesh *pBEdgeMesh= mpTMesh->getBndEdgeMeshID(bnd_id);
    CBoundaryNode *pBNode= pBEdgeMesh->getBNodeID(bnode_id);

    pBNode->addValue(dof, 0, val);
}
// BNode周囲のAggregateEdge情報の初期化
//
void CMeshFactory::initEdgeAggregate(const uiint& mgLevel, const uiint& mesh_id, const uiint& bnd_id)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh_ID(mesh_id);

    // 境界辺メッシュ
    CBoundaryEdgeMesh *pBEdgeMesh= mpTMesh->getBndEdgeMeshID(bnd_id);

    //pBEdgeMesh->resizeAggEdge();
    pBEdgeMesh->setupAggEdge();
}
void CMeshFactory::resizeEdgeAggregate(const uiint& mgLevel, const uiint& mesh_id, const uiint& bnd_id)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh_ID(mesh_id);

    // 境界辺メッシュ
    CBoundaryEdgeMesh *pBEdgeMesh= mpTMesh->getBndEdgeMeshID(bnd_id);

    pBEdgeMesh->resizeAggEdge();
}
// BoundaryEdgeMeshの辺(BoundaryEdge) 生成
// ----
void CMeshFactory::GeneBoundaryEdge(const uiint& mgLevel, const uiint& bnd_id, const uiint& bndType, const uiint& elemType,
        const uiint& mesh_id, const uiint& elem_id, const uiint& edge_id, vuint& vBNodeID,
        const uiint& b_edge_id, const uiint& dof, const double& val)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh(mesh_id);

    // 境界辺メッシュ
    CBoundaryEdgeMesh *pBEdgeMesh= mpTMesh->getBndEdgeMeshID(bnd_id);
    pBEdgeMesh->setBndType(bndType);
    pBEdgeMesh->setID(bnd_id);

    // 境界辺
    // *DOF違いの同一BEdgeがセットされるので,BEdgeIDによる選別.
    // ----
    // カレントBEdgeIDを取得
    // ----
    uiint crIndex= pBEdgeMesh->getNumOfEdge();
    uiint crBEdgeID;
    CBoundaryEdge *pCrBEdge;
    uiint ib;
    bool bfind(false);//新規生成か否かのフラグ
    if(crIndex > 0){
        if(crIndex==1){
            pCrBEdge= pBEdgeMesh->getBEdgeIX(0);
            crBEdgeID= pCrBEdge->getID();

            if(b_edge_id==crBEdgeID) bfind= true;
        }
        for(ib=crIndex-1; ib > 0; ib--){
            pCrBEdge= pBEdgeMesh->getBEdgeIX(ib);
            crBEdgeID= pCrBEdge->getID();

            if(b_edge_id==crBEdgeID) bfind= true;
        };
    }

    CBoundaryEdge *pBEdge;

    if(bfind){
        // 既存のEdgeMeshにBEdgeが存在する場合
        pBEdge= pBEdgeMesh->getBEdgeID(b_edge_id);
        //pBEdge->addDOF(dof);

        if(bndType==BoundaryType::Neumann){
            double dLength= pBEdge->getLength();
            double dBndVal= val * dLength;//分布値*辺長
            pBEdge->setBndValue(dof, dBndVal);
        }

    }else{
        //新規にBEdgeを生成する場合
        pBEdge = new CBoundaryEdge();// <<<<<<<<<<<<<< new

        pBEdge->setElementID(elem_id);
        pBEdge->setElementEdgeID(edge_id);
        pBEdge->setID(b_edge_id);
        //pBEdge->setBndValue(dof,val);//分布値*辺長さ に変更のため辺長さ計算後に移動
        pBEdge->setBEdgeShape(elemType);

        CElement *pElem= mpTMesh->getElement(elem_id);
        pBEdge->setElement(pElem);

        switch(elemType){
            case(ElementType::Beam):case(ElementType::Beam2):
                pBEdge->resizeBNode(2);
                break;
            default:
                //TODO:Logger
                break;
        }

        pBEdgeMesh->addBEdge(pBEdge);
    }

    if(!bfind){
        // 境界Edge両端節点
        CBoundaryNode *pBNode;
        uiint numOfVert= pBEdge->getNumOfVert();
        uiint ivert;
        for(ivert=0; ivert < numOfVert; ivert++){
            pBNode= pBEdgeMesh->getBNodeID(vBNodeID[ivert]);
            pBEdge->setBNode(ivert, pBNode);
        };
        // 2次要素の場合
        if(pBEdge->getOrder()==ElementOrder::Second){
            pBNode= pBEdgeMesh->getBNodeID(vBNodeID[2]);

            // 辺BNodeに対応する、AggEdgeをセット
            //
            uiint ibnode = pBEdgeMesh->getBNodeIndex(vBNodeID[2]);
            pBEdgeMesh->setAggEdge(ibnode, pBEdge->getID());

            pBEdge->setEdgeBNode(pBNode);

            pBEdge->replaceEdgeBNode();//mvBNodeへの移設
        }
        pBEdge->calcLength();
        if(bndType==BoundaryType::Neumann){
            double dLength= pBEdge->getLength();
            double dBndVal= val * dLength;//分布値*辺長
            pBEdge->setBndValue(dof, dBndVal);
        }
    }
}


// ----
// 境界条件階層化
// ----
void CMeshFactory::refineBoundary()
{
    uiint numOfMesh;
    uiint iLevel, iMesh;
    for(iLevel=0; iLevel < mMGLevel; iLevel++){
        
        //cout << "MeshFactory::refineBoundary, iLevel   " << iLevel << endl;

        mpTAssyModel = mpGMGModel->getAssyModel(iLevel);
        CAssyModel *pProgAssy= mpGMGModel->getAssyModel(iLevel+1);

        numOfMesh= mpTAssyModel->getNumOfMesh();
        for(iMesh=0; iMesh < numOfMesh; iMesh++){
            
            mpTMesh = mpTAssyModel->getMesh(iMesh);
            CMesh *pProgMesh= pProgAssy->getMesh(iMesh);

            //cout << "MeshFactory::refineBoundary, iMesh " << iMesh << endl;

            // BoundaryFaceMesh
            // ----
            uiint numOfBFaceMesh= mpTMesh->getNumOfBoundaryFaceMesh();
            pProgMesh->reserveBndFaceMesh(numOfBFaceMesh);
            uiint iBFaceMesh;
            for(iBFaceMesh=0; iBFaceMesh < numOfBFaceMesh; iBFaceMesh++){

                //cout << "MeshFactory::refineBoundary, iBFaceMesh " << iBFaceMesh << endl;
                
                CBoundaryFaceMesh *pBFaceMesh= mpTMesh->getBndFaceMeshIX(iBFaceMesh);
                CBoundaryFaceMesh *pProgBFaceMesh= new CBoundaryFaceMesh;// <<<<<<<<<<<<<<<<< new
                
                pProgMesh->setBndFaceMesh(pProgBFaceMesh);

                //progBFaceMeshへDOFをセット
                pProgBFaceMesh->resizeDOF(pBFaceMesh->getNumOfDOF());
                uiint idof, dof;
                for(idof=0; idof < pBFaceMesh->getNumOfDOF(); idof++){
                    dof = pBFaceMesh->getDOF(idof);
                    pProgBFaceMesh->setDOF(idof, dof);
                }
                
                pBFaceMesh->GeneEdgeBNode();
                pBFaceMesh->GeneFaceBNode();
                
                pBFaceMesh->refine(pProgBFaceMesh);// Refine
                pProgBFaceMesh->resizeAggFace();
                pProgBFaceMesh->setupAggFace();
                
                uiint nBndType= pBFaceMesh->getBndType();
                pProgBFaceMesh->setBndType(nBndType);// Dirichlet.or.Neumann

                uiint nID= pBFaceMesh->getID();
                pProgBFaceMesh->setID(nID);

                pProgBFaceMesh->setMGLevel(iLevel+1);
                pProgBFaceMesh->setMaxMGLevel(mMGLevel);
                
                //    //BNodeへの境界値の再配分
                //    //--
                //    if(BoundaryType::Dirichlet==pBFaceMesh->getBndType()){
                //        pBFaceMesh->distValueBNode();//Dirichlet:親の方でBNodeへの配分を決める
                //    }
                //    if(BoundaryType::Neumann==pBFaceMesh->getBndType()){
                //        if(iLevel==0) pBFaceMesh->distValueBNode();//Level=0の場合は、親側でNeumannも分配
                //        pProgBFaceMesh->distValueBNode();//Neumann:子供の方でBNodeへの配分を決める
                //    }
            };

            // BoundaryEdgeMesh
            // ----
            uiint numOfEdgeMesh= mpTMesh->getNumOfBoundaryEdgeMesh();
            pProgMesh->reserveBndEdgeMesh(numOfEdgeMesh);
            uiint iBEdgeMesh;
            for(iBEdgeMesh=0; iBEdgeMesh < numOfEdgeMesh; iBEdgeMesh++){

                CBoundaryEdgeMesh *pBEdgeMesh= mpTMesh->getBndEdgeMeshIX(iBEdgeMesh);
                
                CBoundaryEdgeMesh *pProgBEdgeMesh= new CBoundaryEdgeMesh;// <<<<<<<<<<<<<< new
                
                pProgMesh->setBndEdgeMesh(pProgBEdgeMesh);

                //ProgBEdgeMeshにDOFをセット
                pProgBEdgeMesh->resizeDOF(pBEdgeMesh->getNumOfDOF());
                uiint idof, dof;
                for(idof=0; idof < pBEdgeMesh->getNumOfDOF(); idof++){
                    dof = pBEdgeMesh->getDOF(idof);
                    pProgBEdgeMesh->setDOF(idof, dof);
                }

                pBEdgeMesh->GeneEdgeBNode();

                pBEdgeMesh->refine(pProgBEdgeMesh);// Refine
                pProgBEdgeMesh->resizeAggEdge();
                pProgBEdgeMesh->setupAggEdge();

                uiint nBndType= pBEdgeMesh->getBndType();
                pProgBEdgeMesh->setBndType(nBndType);// Dirichlet.or.Neumann

                uiint nID= pBEdgeMesh->getID();
                pProgBEdgeMesh->setID(nID);

                pProgBEdgeMesh->setMGLevel(iLevel+1);
                pProgBEdgeMesh->setMaxMGLevel(mMGLevel);
                
                //    //BNodeへの境界値の再配分
                //    //--
                //    if(BoundaryType::Dirichlet==pBEdgeMesh->getBndType()){
                //        pBEdgeMesh->distValueBNode();//Dirichlet:親の方でBNodeへの配分を決める
                //    }
                //    if(BoundaryType::Neumann==pBEdgeMesh->getBndType()){
                //        if(iLevel==0) pBEdgeMesh->distValueBNode();//Level=0の場合は、親側でNeumannも分配
                //        pProgBEdgeMesh->distValueBNode();//Neumann:子供の方でBNodeへの配分を決める
                //    }
            };

            // BoundaryVolumeMesh
            // ----
            uiint numOfVolMesh= mpTMesh->getNumOfBoundaryVolumeMesh();
            pProgMesh->reserveBndVolumeMesh(numOfVolMesh);
            uiint iBVolMesh;
            for(iBVolMesh=0; iBVolMesh < numOfVolMesh; iBVolMesh++){

                CBoundaryVolumeMesh *pBVolMesh= mpTMesh->getBndVolumeMeshIX(iBVolMesh);
                
                CBoundaryVolumeMesh *pProgBVolMesh= new CBoundaryVolumeMesh;// <<<<<<<<<<<<<<<<<< new

                pProgMesh->setBndVolumeMesh(pProgBVolMesh);

                //pProgBVolMeshにDOFをセット
                pProgBVolMesh->resizeDOF(pBVolMesh->getNumOfDOF());
                uiint idof, dof;
                for(idof=0; idof < pBVolMesh->getNumOfDOF(); idof++){
                    dof = pBVolMesh->getDOF(idof);
                    pProgBVolMesh->setDOF(idof, dof);
                }

                pBVolMesh->GeneEdgeBNode();
                pBVolMesh->GeneFaceBNode();
                pBVolMesh->GeneVolBNode();
                
                pBVolMesh->refine(pProgBVolMesh);// Refine
                pProgBVolMesh->resizeAggVol();
                pProgBVolMesh->setupAggVol();

                uiint nBndType= pBVolMesh->getBndType();
                pProgBVolMesh->setBndType(nBndType);// Dirichlet .or. Neumann

                uiint nID= pBVolMesh->getID();
                pProgBVolMesh->setID(nID);

                pProgBVolMesh->setMGLevel(iLevel+1);
                pProgBVolMesh->setMaxMGLevel(mMGLevel);
                
                //    //BNodeへの境界値の再配分
                //    //--
                //    if(BoundaryType::Dirichlet==pBVolMesh->getBndType()){
                //        pBVolMesh->distValueBNode();//Dirichlet:親の方でBNodeへの配分を決める
                //    }
                //    if(BoundaryType::Neumann==pBVolMesh->getBndType()){
                //        if(iLevel==0) pBVolMesh->distValueBNode();//Level=0の場合は、親側でNeumannも分配
                //        pProgBVolMesh->distValueBNode();//Neumann:子供の方でBNOdeへの配分を決める
                //    }
            };

            // BoundaryNodeMesh : BNodeMeshGrp* を各階層にセット.
            // ----
            CBNodeMeshGrp* pBNodeMeshGrp= mpTMesh->getBNodeMeshGrp();
            pProgMesh->setBNodeMeshGrp(pBNodeMeshGrp);

        };// iMesh loop-end

    };// iLevel loop-end


    //cout << "MeshFactory::refineBoundary  ---- A" << endl;


    // 2次要素の場合：辺BNodeを最終レベルに生成
    // ----
    mpTAssyModel = mpGMGModel->getAssyModel(mMGLevel);

    numOfMesh= mpTAssyModel->getNumOfMesh();

    for(iMesh=0; iMesh < numOfMesh; iMesh++){
        mpTMesh = mpTAssyModel->getMesh(iMesh);

        uiint numOfBFaceMesh= mpTMesh->getNumOfBoundaryFaceMesh();
        uiint iBFaceMesh;
        for(iBFaceMesh=0; iBFaceMesh < numOfBFaceMesh; iBFaceMesh++){

            CBoundaryFaceMesh *pBFaceMesh= mpTMesh->getBndFaceMeshIX(iBFaceMesh);

            pBFaceMesh->GeneEdgeBNode();//辺BNode生成

        };//iBFaceMesh loop-end

        uiint numOfBEdgeMesh= mpTMesh->getNumOfBoundaryEdgeMesh();
        uiint iBEdgeMesh;
        for(iBEdgeMesh=0; iBEdgeMesh < numOfBEdgeMesh; iBEdgeMesh++){

            CBoundaryEdgeMesh *pBEdgeMesh= mpTMesh->getBndEdgeMeshIX(iBEdgeMesh);

            pBEdgeMesh->GeneEdgeBNode();//辺BNode生成

        };//iBEdgeMesh loop-end

        uiint numOfBVolMesh= mpTMesh->getNumOfBoundaryVolumeMesh();
        uiint iBVolMesh;
        for(iBVolMesh=0; iBVolMesh < numOfBVolMesh; iBVolMesh++){

            CBoundaryVolumeMesh *pBVolMesh= mpTMesh->getBndVolumeMeshIX(iBVolMesh);

            pBVolMesh->GeneEdgeBNode();//辺BNode生成

        };//iBVolMesh loop-end

    };//iMesh loop-end

    //cout << "MeshFactory::refineBoundary  ---- B" << endl;

    // BNodeへの境界値-分配(0 〜 最終レベル:mMGLevel)
    //
    for(iLevel=0; iLevel < mMGLevel+1; iLevel++){
        mpTAssyModel = mpGMGModel->getAssyModel(iLevel);
        numOfMesh= mpTAssyModel->getNumOfMesh();

        //cout << "MeshFactory::refineBoundary, Dist-Level " << iLevel << endl;

        for(iMesh=0; iMesh < numOfMesh; iMesh++){
            mpTMesh = mpTAssyModel->getMesh(iMesh);

            uiint numOfBFaceMesh= mpTMesh->getNumOfBoundaryFaceMesh();
            uiint iBFaceMesh;
            for(iBFaceMesh=0; iBFaceMesh < numOfBFaceMesh; iBFaceMesh++){

                CBoundaryFaceMesh *pBFaceMesh= mpTMesh->getBndFaceMeshIX(iBFaceMesh);

                ////cout << "MeshFactory::refineBoundary, BFace distValueBNode ---(1) " << endl;
                pBFaceMesh->distValueBNode();
                ////cout << "MeshFactory::refineBoundary, BFace distValueBNode ---(2) " << endl;

            };//iBFaceMesh loop-end

            uiint numOfBEdgeMesh= mpTMesh->getNumOfBoundaryEdgeMesh();
            uiint iBEdgeMesh;
            for(iBEdgeMesh=0; iBEdgeMesh < numOfBEdgeMesh; iBEdgeMesh++){

                CBoundaryEdgeMesh *pBEdgeMesh= mpTMesh->getBndEdgeMeshIX(iBEdgeMesh);

                ////cout << "MeshFactory::refineBoundary, BEdge distValueBNode ---(1) " << endl;
                pBEdgeMesh->distValueBNode();
                ////cout << "MeshFactory::refineBoundary, BEdge distValueBNode ---(2) " << endl;

            };//iBEdgeMesh loop-end

            uiint numOfBVolMesh= mpTMesh->getNumOfBoundaryVolumeMesh();
            uiint iBVolMesh;
            for(iBVolMesh=0; iBVolMesh < numOfBVolMesh; iBVolMesh++){

                CBoundaryVolumeMesh *pBVolMesh= mpTMesh->getBndVolumeMeshIX(iBVolMesh);

                ////cout << "MeshFactory::refineBoundary, BVol distValueBNode ---(1) " << endl;
                pBVolMesh->distValueBNode();
                ////cout << "MeshFactory::refineBoundary, BVol distValueBNode ---(2) " << endl;

            };//iBVolMesh loop-end
        };
    };

    //cout << "MeshFactory::refineBoundary  ---- C" << endl;
}



//----
// Bucket in Mesh
//----

// Node_Index set to Bucket(in Mesh):: All in One Method
//
void CMeshFactory::setupBucketNode(const uiint& mgLevel, const uiint& mesh_id)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh(mesh_id);

    mpTMesh->setupBucketNode();// Bucket一括処理

}
// initialize Bucket for Node
void CMeshFactory::initBucketNode(const uiint& mgLevel, const uiint& mesh_id, const uiint& maxID, const uiint& minID)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh(mesh_id);

    mpTMesh->initBucketNode(maxID, minID);// Bucket領域確保
}
// set (ID & Index) to Bucket for Node
void CMeshFactory::setIDBucketNode(const uiint& mgLevel, const uiint& mesh_id, const uiint& id, const uiint& index)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh(mesh_id);

    mpTMesh->setupBucketNodeIndex(id, index);

}

// Element_Index set to Bucket (in Mesh):: All in-One Method
//
void CMeshFactory::setupBucketElement(const uiint& mgLevel, const uiint& mesh_id)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh(mesh_id);

    mpTMesh->setupBucketElement();
}
// initialize Bucket for Element
void CMeshFactory::initBucketElement(const uiint& mgLevel, const uiint& mesh_id, const uiint& maxID, const uiint& minID)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh(mesh_id);
    ;
    mpTMesh->initBucketElement(maxID, minID);
}
// set (ID & Index) to Bucket for Element
void CMeshFactory::setIDBucketElement(const uiint& mgLevel, const uiint& mesh_id, const uiint& id, const uiint& index)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh(mesh_id);

    mpTMesh->setupBucketElementIndex(id, index);
}

//void CMeshFactory::setMaxMinID_TMeshN(const uint &maxID_Node, const uint &minID_Node)
//{
//    mpTBucket->resizeBucketNode(maxID_Node, minID_Node);
//}
//
//// Element index set for mpTBucket
////
//void CMeshFactory::setMaxMinID_TMeshE(const uint &maxID_Elem, const uint &minID_Elem)
//{
//    mpTBucket->resizeBucketElement(maxID_Elem, minID_Elem);
//}


// 材質データの配列の確保
// --
void CMeshFactory::reserveMaterial(const uiint& res_size)
{
     mpGMGModel->reserveMaterial(res_size);
}

// 材質データの生成
// --
void CMeshFactory::GeneMaterial(const uiint& mesh_id, const uiint& material_id, string& name, vuint& vType, vdouble& vValue)
{
    CMaterial *pMaterial = new CMaterial;

    pMaterial->setID(material_id);
    pMaterial->setName(name);
    pMaterial->setMeshID(mesh_id);

    for(uiint i=0; i< vType.size(); i++) pMaterial->setValue(vType[i],vValue[i]);

    
    mpGMGModel->setMaterial(pMaterial);
}




// 通信領域(CommMesh)の配列確保
//
void CMeshFactory::reserveCommMesh(const uiint& mgLevel, const uiint& mesh_id, const uiint& res_size)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh(mesh_id);

    mpTMesh->reserveCommMesh(res_size);
}

// 通信領域(CommMesh)の生成
//
void CMeshFactory::GeneCommMesh(const uiint& mgLevel, const uiint& mesh_id, const uiint& comID, const uiint& myRank, const uiint& nTransmitRank)
{
    mpTAssyModel = mpGMGModel->getAssyModel(mgLevel);
    mpTMesh = mpTAssyModel->getMesh(mesh_id);

    CIndexBucket *pBucket= mpTMesh->getBucket();

    mpTCommMesh = new CCommMesh(pBucket);
    mpTCommMesh->setCommID(comID);
    mpTCommMesh->setRankID(myRank);
    mpTCommMesh->setTransmitRankID(nTransmitRank);

    
    mpTMesh->setCommMesh(mpTCommMesh);
}

// 通信ノード(CommMesh内に,MeshのNodeポインターの配列を確保)
//  => CommNodeというものは,存在しない.
//
void CMeshFactory::reserveCommNode(const uiint& mgLevel, const uiint& mesh_id, const uiint& commesh_id, const uiint& res_size)
{
    mpTAssyModel= mpGMGModel->getAssyModel(mgLevel);
    mpTMesh= mpTAssyModel->getMesh(mesh_id);

    mpTCommMesh= mpTMesh->getCommMesh(commesh_id);
    
    mpTCommMesh->reserveNode(res_size);   //CommMeshノードの配列予約
    mpTCommMesh->resizeNodeRank(res_size);//NodeRank一覧の確保

}

// MeshからNodeを取得してCommMeshにセット
//  => CommNodeというものは,存在しない.
//
void CMeshFactory::GeneCommNode(const uiint& mgLevel, const uiint& commNodeID,
                                     const uiint& mesh_id, const uiint& commesh_id, const uiint& nodeID, const uiint& rank)
{
    mpTAssyModel= mpGMGModel->getAssyModel(mgLevel);
    mpTMesh= mpTAssyModel->getMesh(mesh_id);

    mpTCommMesh= mpTMesh->getCommMesh(commesh_id);

    // MeshからNodeを取得して,CommMeshにセット
    // --
    CNode* pNode= mpTMesh->getNode(nodeID);
    mpTCommMesh->setNode(pNode);// 順番にpush_backしている.
    mpTCommMesh->setNodeRank(commNodeID, rank);//Nodeランク一覧 配列

    // Send,Recvノードのセット
    if(mpTCommMesh->getRankID()==rank) mpTCommMesh->setSendNode(pNode, commNodeID);
    if(mpTCommMesh->getTransmitRankID()==rank) mpTCommMesh->setRecvNode(pNode, commNodeID);
}

// 通信要素(CommElement)
//
void CMeshFactory::reserveCommElement(const uiint& mgLevel, const uiint& mesh_id, const uiint& commesh_id, const uiint& res_size)
{
    mpTAssyModel= mpGMGModel->getAssyModel(mgLevel);
    mpTMesh= mpTAssyModel->getMesh(mesh_id);

    mpTCommMesh= mpTMesh->getCommMesh(commesh_id);
    mpTCommMesh->reserveCommElementAll(res_size);
}

// CommElementを生成して,MeshからElementを取得してセット
//
void CMeshFactory::GeneCommElement(const uiint& mgLevel, const uiint& mesh_id, const uiint& commesh_id,
                                   const uiint& nType, const uiint& elemID, vuint& vCommNodeID)
{
    mpTAssyModel= mpGMGModel->getAssyModel(mgLevel);
    mpTMesh= mpTAssyModel->getMesh(mesh_id);

    mpTCommMesh= mpTMesh->getCommMesh(commesh_id);
    
    uiint numOfVert= vCommNodeID.size();
    vuint vNodeRank;  vNodeRank.reserve(numOfVert);
    
    uiint ivert, rank;
    uiint commNodeID;
    for(ivert=0; ivert< numOfVert; ivert++){
        commNodeID= vCommNodeID[ivert];
        rank= mpTCommMesh->getNodeRank(commNodeID);
        
        vNodeRank.push_back(rank);
    };
    
    CCommElement *pCommElem;
    switch(nType){
        case(ElementType::Hexa):
            pCommElem = new CCommHexa;
            break;
        case(ElementType::Tetra):
            pCommElem = new CCommTetra;
            break;
        case(ElementType::Prism):
            pCommElem = new CCommPrism;
            break;
//        case(ElementType::Pyramid):
//            pCommElem = new CCommPyramid;
//            break;
        case(ElementType::Quad):
            pCommElem = new CCommQuad;
            break;
        case(ElementType::Triangle):
            pCommElem = new CCommTriangle;
            break;
        case(ElementType::Beam):
            pCommElem = new CCommBeam;
            break;
    }
    
    CElement* pElem= mpTMesh->getElement(elemID);

    //cout << "GeneCommElement  pElem ID==" << pElem->getID() << endl;

    pCommElem->setElement(pElem);
    
    // Nodeランクのセット
    // --
    for(ivert=0; ivert< numOfVert; ivert++){
        rank = vNodeRank[ivert];
        pCommElem->setNodeRank(ivert, rank);
    };
    
    mpTCommMesh->setCommElementAll(pCommElem);
    
//
//    prolongation時の処理 Send,Recvの割り振りがファイル入力時に行われていないため参考.
//
//    //1.CommMesh内でのCommElemのIndex番号の割り振り && CommMesh内のCommElementを通信するか否かのCommElementに選別
//    pProgCommMesh->AllocateCommElement();
//
//    //2.CommMesh内でのCommElemの隣接情報
//    //3.CommMesh内でのNodeのIndex番号の割り振り,CommMeshのmvNode,mvSendNode,mvRecvNodeの取得
//    pProgCommMesh->setupAggCommElement(pProgMesh->getElements());
//    pProgCommMesh->sortCommNodeIndex();// CommMesh内でのNode Index番号生成, Send,Recvノードの選別, DNode,DElementの選別ソート
//                                       // mvNodeのセットアップもsortCommNodeIndexから,CommElementのsetCommNodeIndex内でmvNodeにセットしている.
//    //4.mapデータのセットアップ
//    pProgCommMesh->setupMapID2CommID();
}

//// 通信領域のprolongation
//// ○ refineMesh();後に呼ばれるルーチン
//// --
//void CMeshFactory::refineCommMesh()
//{
//    //-- Mesh
//    CAssyModel *pAssy, *pProgAssy;
//    CMesh      *pMesh, *pProgMesh;
//    //-- 通信Mesh
//    CCommMesh  *pCommMesh, *pProgCommMesh;
//    //-- 通信要素(CommElem)
//    CCommElement      *pCommElem;//元になったCommElem(親のCommElem)
//    vector<CCommElement*> vProgCommElem;//生成されるprogCommElem達
//    CCommElement         *pProgCommElem;//progCommElem <= prolongation CommElem
//
//
//    uint numOfMesh, numOfCommMesh, numOfCommElemAll, numOfCommNode;
//    uint ilevel,imesh,icommesh,icomelem,iprocom,ivert;
//    // ---
//    // 階層Level ループ
//    // ---
//    for(ilevel=0; ilevel< mMGLevel; ilevel++){
//
//        //debug
//        cout << "ilevel => " << ilevel << endl;
//
//        pAssy= mpGMGModel->getAssyModel(ilevel);
//        pProgAssy= mpGMGModel->getAssyModel(ilevel+1);
//
//        numOfMesh= pAssy->getNumOfMesh();
//
//        ////debug
//        //cout << "numOfMesh =>" << numOfMesh << endl;
//
//        // ---
//        // Mesh(パーツ) ループ in AssyMode
//        // ---
//        for(imesh=0; imesh< numOfMesh; imesh++){
//            pMesh= pAssy->getMesh(imesh);
//            pProgMesh= pProgAssy->getMesh(imesh);
//
//            numOfCommMesh= pMesh->getNumOfCommMesh();
//            ////debug
//            //cout << "numOfCommMesh => " << numOfCommMesh << endl;
//
//            // --
//            // CommMesh(通信領域) ループ in Mesh
//            // --
//            for(icommesh=0; icommesh< numOfCommMesh; icommesh++){
//                pCommMesh= pMesh->getCommMesh(icommesh);
//
//                // "new CommMesh" に下段階層のプロパティ・セット
//                // --
//                pProgCommMesh= new CCommMesh;// <<<<<<<<<<<-- prolongation CommMesh
//                pProgCommMesh->setCommID( pCommMesh->getCommID());
//                pProgCommMesh->setRankID( pCommMesh->getRankID());
//                pProgCommMesh->setTransmitRankID( pCommMesh->getTransmitRankID());
//
//                pProgMesh->setCommMesh(pProgCommMesh);// プロパティを全てセットしてからMeshにCommMeshをセットすること.
//
//
//                numOfCommElemAll= pCommMesh->getNumOfCommElementAll();
//                pProgCommMesh->reserveCommElementAll(numOfCommElemAll*8);// <<<<<<-- CommElemAllリザーブ
//
//                numOfCommNode= pCommMesh->getNumOfNode();
//                pProgCommMesh->reserveNode(numOfCommNode*8);// <<<<<<<<<<<<-- CommMeshノード リザーブ
//
//                //debug
//                string sOutputStr =  boost::lexical_cast<string>(pCommMesh->getCommID()) + ","
//                                    +boost::lexical_cast<string>(pCommMesh->getRankID()) + ","
//                                    +boost::lexical_cast<string>(pCommMesh->getTransmitRankID()) + ","
//                                    +boost::lexical_cast<string>(numOfCommElemAll);
//                //debug
//                mpLogger->Info(Utility::LoggerMode::Debug,"setup to progCommMesh:commID,rank,transmit_rank,CommElemAll数 => ",sOutputStr);
//
//
//                //pCommMeshからCommElemを取得し,progCommElemを生成 => progCommMeshにセット
//                // 計算rank(計算領域番号)は,CommMeshが所有.
//                for(icomelem=0; icomelem< numOfCommElemAll; icomelem++){
//
//                    pCommElem= pCommMesh->getCommElementAll(icomelem);
//                    pCommElem->setupProgNodeRank(ilevel+1);//辺,面,体積中心にRank設定. <<<<<<-- progCommElemのNodeランク
//
//                    vProgCommElem.clear();
//
//                    //debug
//                    mpLogger->Info(Utility::LoggerMode::Debug,"commelem index=>", icomelem);
//
//                    // 形状別のprogCommElem
//                    switch(pCommElem->getShapeType()){
//                        case(ElementType::Hexa):
//                            vProgCommElem.reserve(8);
//                            for(ivert=0; ivert< 8; ivert++){
//                                pProgCommElem= new CCommHexa;// <<<<<<<<<-- prolongation CommElement
//                                vProgCommElem.push_back(pProgCommElem);
//                            };
//                            dividCommElem(pCommElem, vProgCommElem);
//
//                            //debug
//                            mpLogger->Info(Utility::LoggerMode::Debug,"CommElem(Hexa)の分割");
//
//                            break;
//                        case(ElementType::Tetra):
//                            vProgCommElem.reserve(4);
//                            for(ivert=0; ivert< 4; ivert++){
//                                pProgCommElem= new CCommHexa;// <<<<<<<<<-- prolongation CommElement
//                                vProgCommElem.push_back(pProgCommElem);
//                            };
//                            dividCommElem(pCommElem, vProgCommElem);
//
//                            break;
//                        case(ElementType::Prism):
//                            vProgCommElem.reserve(6);
//                            for(ivert=0; ivert< 6; ivert++){
//                                pProgCommElem= new CCommHexa;// <<<<<<<<<-- prolongation CommElement
//                                vProgCommElem.push_back(pProgCommElem);
//                            };
//                            dividCommElem(pCommElem, vProgCommElem);
//
//                            break;
//                        case(ElementType::Pyramid):
//                            // Pyramid => Hexa縮退 ?
//                            vProgCommElem.reserve(8);
//                            for(ivert=0; ivert< 8; ivert++){
//                                pProgCommElem= new CCommHexa;// <<<<<<<<<-- prolongation CommElement
//                                vProgCommElem.push_back(pProgCommElem);
//                            };
//                            dividCommElem(pCommElem, vProgCommElem);
//
//                            break;
//                        case(ElementType::Quad):
//                            vProgCommElem.reserve(4);
//                            for(ivert=0; ivert< 4; ivert++){
//                                pProgCommElem= new CCommQuad;// <<<<<<<<<-- prolongation CommElement
//                                vProgCommElem.push_back(pProgCommElem);
//                            };
//                            dividCommElem(pCommElem, vProgCommElem);
//
//                            break;
//                        case(ElementType::Triangle):
//                            vProgCommElem.reserve(3);
//                            for(ivert=0; ivert< 3; ivert++){
//                                pProgCommElem= new CCommQuad;// <<<<<<<<<-- prolongation CommElement
//                                vProgCommElem.push_back(pProgCommElem);
//                            };
//                            dividCommElem(pCommElem, vProgCommElem);
//
//                            break;
//                        case(ElementType::Beam):
//                            vProgCommElem.reserve(2);
//                            for(ivert=0; ivert< 2; ivert++){
//                                pProgCommElem= new CCommBeam;// <<<<<<<<<-- prolongation CommElement
//                                vProgCommElem.push_back(pProgCommElem);
//                            };
//                            dividCommElem(pCommElem, vProgCommElem);
//
//                            break;
//                        default:
//                            mpLogger->Info(Utility::LoggerMode::Error, "refineCommMesh内の,CommElement ShapeType Error @MeshFactory::refineCommMesh");
//                            break;
//                    }
//                    // progCommMeshへprogCommElmeAllのセット(全てのCommElement)
//                    // --
//                    for(iprocom=0; iprocom< vProgCommElem.size(); iprocom++){
//                        pProgCommMesh->setCommElementAll(vProgCommElem[iprocom]);
//                    };
//
//                    //debug
//                    mpLogger->Info(Utility::LoggerMode::Debug, "refineCommMesh内の,pProgCommMesh->setCommElem");
//
//                };//CommElem ループ
//
//                // 1.CommMesh内でのCommElemのIndex番号の割り振り && CommMesh内のCommElementを通信するか否かのCommElementに選別
//                pProgCommMesh->AllocateCommElement();
//
//                //debug
//                cout << "pProgCommMesh->AllocateCommElement" << endl;
//
//                // 2.CommMesh内でのCommElemの隣接情報
//                // 3.CommMesh内でのNodeのIndex番号の割り振り,CommMeshのmvNode,mvSendNode,mvRecvNodeの取得
//                //
//                pProgCommMesh->setupAggCommElement(pProgMesh->getElements());
//                //debug
//                cout << "pProgCommMesh->setupAggCommElement" << endl;
//
//                pProgCommMesh->sortCommNodeIndex();// CommMesh内でのNode Index番号生成, Send,Recvノードの選別, DNode,DElementの選別ソート
//                                                   // mvNodeのセットアップもsortCommNodeIndexから,CommElementのsetCommNodeIndex内でmvNodeにセットしている.
//                //debug
//                cout << "pProgCommMesh->sortCommNodeIndex" << endl;
//
//                // 4.mapデータのセットアップ
//                pProgCommMesh->setupMapID2CommID();
//
//                //debug
//                mpLogger->Info(Utility::LoggerMode::Debug, "refineCommMesh内の,pProgCommMesh->Allocate..setupMapID2CommID");
//
//            };//CommMesh ループ
//
//            // Mesh のNode,Elementの計算領域整理
//            // --
//            pProgMesh->sortMesh();//MeshのmvNode,mvElementから計算に使用しないNode(DNode),Element(DElement)を移動
//
//
//        };//Meshループ
//    };//Levelループ
//}

// prolongation CommElementの生成
// --
void CMeshFactory::GeneProgCommElem(CCommElement* pCommElem, vector<CCommElement*>& vProgCommElem)
{
    CCommElement *pProgCommElem;
    uiint ivert;
    
    // 形状別のprogCommElem
    switch(pCommElem->getShapeType()){
        case(ElementType::Hexa):
            vProgCommElem.reserve(8);
            for(ivert=0; ivert< 8; ivert++){
                pProgCommElem= new CCommHexa;// <<<<<<<<<-- prolongation CommElement
                vProgCommElem.push_back(pProgCommElem);
            };
            dividCommElem(pCommElem, vProgCommElem);

            //debug
            mpLogger->Info(Utility::LoggerMode::Debug,"CommElem(Hexa)の分割");

            break;
        case(ElementType::Tetra):
            vProgCommElem.reserve(4);
            for(ivert=0; ivert< 4; ivert++){
                pProgCommElem= new CCommHexa;// <<<<<<<<<-- prolongation CommElement
                vProgCommElem.push_back(pProgCommElem);
            };
            dividCommElem(pCommElem, vProgCommElem);

            break;
        case(ElementType::Prism):
            vProgCommElem.reserve(6);
            for(ivert=0; ivert< 6; ivert++){
                pProgCommElem= new CCommHexa;// <<<<<<<<<-- prolongation CommElement
                vProgCommElem.push_back(pProgCommElem);
            };
            dividCommElem(pCommElem, vProgCommElem);

            break;
//        case(ElementType::Pyramid):
//            // Pyramid => Hexa縮退 ?
//            vProgCommElem.reserve(8);
//            for(ivert=0; ivert< 8; ivert++){
//                pProgCommElem= new CCommHexa;// <<<<<<<<<-- prolongation CommElement
//                vProgCommElem.push_back(pProgCommElem);
//            };
//            dividCommElem(pCommElem, vProgCommElem);
//
//            break;
        case(ElementType::Quad):
            vProgCommElem.reserve(4);
            for(ivert=0; ivert< 4; ivert++){
                pProgCommElem= new CCommQuad;// <<<<<<<<<-- prolongation CommElement
                vProgCommElem.push_back(pProgCommElem);
            };
            dividCommElem(pCommElem, vProgCommElem);

            break;
        case(ElementType::Triangle):
            vProgCommElem.reserve(3);
            for(ivert=0; ivert< 3; ivert++){
                pProgCommElem= new CCommQuad;// <<<<<<<<<-- prolongation CommElement
                vProgCommElem.push_back(pProgCommElem);
            };
            dividCommElem(pCommElem, vProgCommElem);

            break;
        case(ElementType::Beam):
            vProgCommElem.reserve(2);
            for(ivert=0; ivert< 2; ivert++){
                pProgCommElem= new CCommBeam;// <<<<<<<<<-- prolongation CommElement
                vProgCommElem.push_back(pProgCommElem);
            };
            dividCommElem(pCommElem, vProgCommElem);

            break;
        default:
            mpLogger->Info(Utility::LoggerMode::Error, "ShapeType Error @MeshFactory::GeneProgCommElem");
            break;
    }
}

// 1.再分割CommElement(progCommElem)へ,再分割Elementをセット
// 2.再分割CommElement(progCommElem)の頂点へ"rank"をセット
// --
void CMeshFactory::dividCommElem(CCommElement* pCommElem, vector<CCommElement*>& vProgCommElem)
{
    CProgElementTree *pProgTree = CProgElementTree::Instance();

    CElement* pElem= pCommElem->getElement();
    CElement* pProgElem;
    CCommElement* pProgCommElem;
    uiint nRank;
    uiint ivert, progvert;
    uiint iedge, iface;
    uiint invalid= pProgTree->getInvalidNum();

    ////debug
    //mpLogger->Info(Utility::LoggerMode::Debug,"Factory::dividCommElemの invalid => ",invalid);

    uiint numOfVert, numOfEdge, numOfFace;
    numOfVert= pElem->getNumOfNode(); numOfEdge= pElem->getNumOfEdge(); numOfFace= pElem->getNumOfFace();

    // 親CommElemの頂点ループ(子CommElemのアドレス)
    for(ivert=0; ivert< numOfVert; ivert++){
        pProgElem= pElem->getProgElem(ivert);

        pProgCommElem= vProgCommElem[ivert];
        pProgCommElem->setElement(pProgElem);// progCommElemへ,子要素(progElment)のセット

        // progCommElemの各頂点へ,rank(DomID)のセット
        // --

        // 親のvertexのrankを子のvertexへセット
        progvert= pProgTree->getVertProgVert(ivert, pCommElem->getShapeType());
        nRank= pCommElem->getNodeRank(ivert);
        pProgCommElem->setNodeRank(progvert,nRank);
        ////debug
        //mpLogger->Info(Utility::LoggerMode::Debug,"Factory::dividCommElemのVertexRank=>setNodeRank",(uint)nRank);


        // edgeノードに対応するprogCommElemのrank
        for(iedge=0; iedge< numOfEdge; iedge++){
            
            progvert= pProgTree->getEdgeProgVert(iedge, ivert, pCommElem->getShapeType());

            if(progvert != invalid){
                nRank= pCommElem->getEdgeRank(iedge);
                pProgCommElem->setNodeRank(progvert, nRank);

                ////debug
                //mpLogger->Info(Utility::LoggerMode::Debug,"Factory::dividCommElemのEdgeRank=>setNodeRank",(uint)nRank);
            }
        };
        
        
        // faceノードに対応するprogCommElemのrank
        for(iface=0; iface< numOfFace; iface++){
            
            progvert= pProgTree->getFaceProgVert(iface, ivert, pCommElem->getShapeType());

            if(progvert != invalid){
                nRank= pCommElem->getFaceRank(iface);
                pProgCommElem->setNodeRank(progvert, nRank);

                ////debug
                //mpLogger->Info(Utility::LoggerMode::Debug,"Factory::dividCommElemのFaceRank=>setNodeRank",(uint)nRank);
            }
        };
        // volumeに対応するprogCommElemのrank
        progvert= pProgTree->getVolProgVert(ivert, pCommElem->getShapeType());
        nRank= pCommElem->getVolRank();
        pProgCommElem->setNodeRank(progvert, nRank);
        ////debug
        //mpLogger->Info(Utility::LoggerMode::Debug,"Factory::dividCommElemのVolRank=>setNodeRank",(uint)nRank);
    };
}




// [コンタクトメッシュ数ぶん呼び出される]
//  -------------------------------
// ファイル入力時にレベル0のContactMesh要素へのマーキング
//
//  -> 全レベルのContactMeshは,ここで予め生成しておく -> CMW::Refine の初期処理に移設
//
//  -> 自身と同じランクに所属する接合面は, setupContactMesh, setupSkin で生成される.
// --
// コンタクトメッシュを全階層に生成
// --
void CMeshFactory::GeneContactMesh(const uiint& contactID, const uiint& myRank, const uiint& transRank, const uiint& nProp)
{
    uiint ilevel=0;
    ////for(ilevel=0; ilevel< mMGLevel+1; ilevel++){
        mpTAssyModel= mpGMGModel->getAssyModel(ilevel);

        CContactMesh *pContactMesh= new CContactMesh;// 接合メッシュの生成
        pContactMesh->setID(contactID);
        pContactMesh->setLevel(ilevel);

        pContactMesh->setRank(myRank);
        pContactMesh->setTransmitRank(transRank);

        pContactMesh->setProp(nProp);
        
        mpTAssyModel->addContactMesh(pContactMesh, contactID);
    ////};
}
// コンタクトノードの生成(Level==0)
//
void CMeshFactory::GeneContactNode(const uiint& mgLevel, const uiint& contactID, const uiint& conNodeID, const vdouble& vCoord,
        const string& s_param_type, const uiint& numOfVector, const uiint& numOfScalar,
        bool bmesh, const uiint& meshID, const uiint& nodeID,
        const uiint& rank, const uiint& maslave)
{
    mpTAssyModel= mpGMGModel->getAssyModel(mgLevel);

    CContactMesh *pConMesh= mpTAssyModel->getContactMesh_ID(contactID);
    
    CContactNode *pConNode= new CContactNode;
    pConNode->setLevel(mgLevel);
    pConNode->pushLevelMarking();//2010.05.27
    pConNode->setID(conNodeID);
    pConNode->setCoord(vCoord);
    if(bmesh){ pConNode->setMeshID(meshID); pConNode->markingSelfMesh();}
    if(bmesh){ pConNode->setNodeID(nodeID); pConNode->markingSelfNode();}
    pConNode->setRank(rank);

    if(s_param_type=="v" || s_param_type=="V" || s_param_type=="sv" || s_param_type=="SV"){
        pConNode->resizeDisp(numOfVector);
        pConNode->initDisp();
    }
    if(s_param_type=="s" || s_param_type=="S" || s_param_type=="sv" || s_param_type=="SV"){
        pConNode->resizeScalar(numOfVector);
        pConNode->initScalar();
    }

    pConMesh->addConNode(pConNode, conNodeID);

    switch(maslave){
    case(0)://マスターConNode
        pConMesh->addMasterConNode(pConNode,conNodeID);
        break;
    case(1)://スレーブConNode
        pConMesh->addSlaveConNode(pConNode,conNodeID);
        break;
    default:
        break;
    }
}

// マスター面の生成(Level==0)
//
//
void CMeshFactory::GeneMasterFace(const uiint& contactID, const uiint& shapeType, const uiint& masterFaceID,
        bool bmesh, const uiint& meshID, const uiint& elemID, const uiint& elemFaceID,
        const vuint& vConNodeID, const uiint& face_rank)
{
    // レベル0の,マスター&スレーブ要素をマーキング
    //  -> レベル0以外は,dividHexa()等でマーキング
    //
    uiint mgLevel(0);
    mpTAssyModel= mpGMGModel->getAssyModel(mgLevel);
    CSkinFace *pMFace= new CMasterFace;

    //初期化
    pMFace->setShapeType(shapeType);

    // bmesh がtrueの場合,自身のMeshの表面がSkinFaceになっているので,マーキングする.
    if(bmesh){
        CMesh *pMMesh;
        pMMesh= mpTAssyModel->getMesh_ID(meshID);
        
        CElement *pMasterElem;
        pMasterElem= pMMesh->getElement(elemID);
        pMasterElem->markingMPCMaster();
        pMasterElem->markingMPCFace(elemFaceID);
        
        pMFace->markingSelf();//自身のMeshの表面メッシュであることをマーキング
        pMFace->setMeshID(meshID);
        pMFace->setElementID(elemID);
        pMFace->setFaceID(elemFaceID);
    }
    
    CContactMesh *pConMesh= mpTAssyModel->getContactMesh_ID(contactID);
    
    pMFace->setID(masterFaceID);
    pMFace->setRank(face_rank);
    pMFace->setLevel(mgLevel);// mgLevel=0 入力レベル

    uiint icnode, numOfConNode = vConNodeID.size();
    for(icnode=0; icnode< numOfConNode; icnode++){
        CContactNode *pConNode= pConMesh->getContactNode_ID(vConNodeID[icnode]);
        pMFace->addNode(pConNode);
    };
    //2次要素の場合、辺ノードをセットしておく
    if(pMFace->getOrder()==ElementOrder::Second){
        uiint nNumOfEdge= pMFace->getNumOfEdge();
        uiint nNumOfVert= pMFace->getNumOfVert();
        for(uiint iedge=0; iedge < nNumOfEdge; iedge++){
            CContactNode *pConNode= pMFace->getNode(nNumOfVert + iedge);
            pMFace->setEdgeConNode(pConNode, iedge);
            pMFace->markingEdgeNode(iedge);
        };
    }


    pConMesh->addMasterFace(pMFace);
}
// スレーブ面の生成(Level==0)
//
//
void CMeshFactory::GeneSlaveFace(const uiint& contactID, const uiint& shapeType, const uiint& slaveFaceID,
        bool bmesh, const uiint& meshID, const uiint& elemID, const uiint& elemFaceID,
        const vuint& vConNodeID, const uiint& face_rank)
{
    // レベル0の,マスター&スレーブ要素をマーキング
    //  -> レベル0以外は,dividHexa()等でマーキング
    //
    uiint mgLevel(0);
    mpTAssyModel= mpGMGModel->getAssyModel(mgLevel);
    CSkinFace *pSFace= new CSkinFace;

    //初期化
    pSFace->setShapeType(shapeType);

    // bmesh がtrueの場合,自身のMeshの表面がSkinFaceになっているので,マーキングする.
    if(bmesh){
        CMesh *pSMesh;
        pSMesh= mpTAssyModel->getMesh_ID(meshID);
        
        CElement *pSlaveElem;
        pSlaveElem= pSMesh->getElement(elemID);
        pSlaveElem->markingMPCSlave();
        pSlaveElem->markingMPCFace(elemFaceID);

        pSFace->markingSelf();//自身のMeshの表面メッシュであることをマーキング
        pSFace->setMeshID(meshID);
        pSFace->setElementID(elemID);
        pSFace->setFaceID(elemFaceID);
    }

    CContactMesh *pConMesh= mpTAssyModel->getContactMesh_ID(contactID);
    
    pSFace->setID(slaveFaceID);
    pSFace->setRank(face_rank);
    pSFace->setLevel(mgLevel);// mgLevel=0 入力レベル
    
    uiint icnode, numOfConNode = vConNodeID.size();
    for(icnode=0; icnode< numOfConNode; icnode++){
        CContactNode *pConNode= pConMesh->getContactNode_ID(vConNodeID[icnode]);
        pSFace->addNode(pConNode);
    };
    //2次要素の場合、辺ノードをセットしておく
    if(pSFace->getOrder()==ElementOrder::Second){
        uiint nNumOfEdge= pSFace->getNumOfEdge();
        uiint nNumOfVert= pSFace->getNumOfVert();
        for(uiint iedge=0; iedge < nNumOfEdge; iedge++){
            CContactNode *pConNode= pSFace->getNode(nNumOfVert + iedge);
            pSFace->setEdgeConNode(pConNode, iedge);
            pSFace->markingEdgeNode(iedge);
        };
    }

    pConMesh->addSlaveFace(pSFace);
}

// MPC接触Mesh生成(全ての階層Level) : マスター面,スレーブ面のID番号の管理
//  -> Level=0のマスター & スレーブはセット済み.
//  -> Level=0から始めてprogMeshを利用する.
// --
void CMeshFactory::refineContactMesh()
{
    // Meshリファイン後に処理
    // --
    // ContactMeshをリファイン.
    // --
    CAssyModel  *pAssy,*pProgAssy;
    CContactMesh *pConMesh,*pProgConMesh;
    CSkinFace  *pSkinFace;
    vector<CSkinFace*> vProgFace;//refineで生成されるSkinFaceの子供
    uiint maslave;                //マスター,スレーブ切り替えINDEX

    uiint meshID,elemID;
    CMesh *pMesh;
    CElement *pElem;

    uiint faceID;//progFaceのID番号生成用途
    uiint maxLayer;//Octreeの最上位レイヤー
    // ----
    // progAssyのContactMeshにRefineしたContactMeshをセットしていくので,ループは < mMGLevel となる.
    // ----
    uiint ilevel;
    for(ilevel=0; ilevel< mMGLevel; ilevel++){
        
        pAssy =  mpGMGModel->getAssyModel(ilevel);    //カレントレベル
        pProgAssy= mpGMGModel->getAssyModel(ilevel+1);//上段のレベル
        
//        cout << "MeshFactory::refineContactMesh, iLevel=" << ilevel << endl;

        uiint numOfCont= pAssy->getNumOfContactMesh();
        uiint icont;
        for(icont=0; icont< numOfCont; icont++){

            pConMesh= pAssy->getContactMesh(icont);
            pProgConMesh= pProgAssy->getContactMesh(icont);

//            cout << "MeshFactory::refineContactMesh --- A" << endl;

            pConMesh->setupCoarseConNode(pProgConMesh);      //現在LevelのConMeshのノードを上位のConMeshに丸ごとセット
            pConMesh->setupAggSkinFace();                    //ContactNode周囲のSkinFaceIDを収集
            pConMesh->setupEdgeConNode(pProgConMesh, ilevel);//辺ノードの生成,辺接続Faceのセット,新ノードをprogConMeshに追加,IDカウント
            pConMesh->setupFaceConNode(pProgConMesh);        //面ノードの生成,新ノードをprogConMeshに追加,IDカウント

//            cout << "MeshFactory::refineContactMesh --- B" << endl;
            
            uiint numOfSkinFace;
            uiint iface;
            //マスター,スレーブ切り替えループ
            for(maslave=0; maslave< 2; maslave++){
                faceID=0;//新たな面IDカウンター(mvLevel別,ContactMesh別,マスター&スレーブ別なので,ここで"0"初期化)
                
                if(maslave==0) numOfSkinFace= pConMesh->getNumOfMasterFace();//マスター面数
                if(maslave==1) numOfSkinFace= pConMesh->getNumOfSlaveFace(); //スレーブ面数

                for(iface=0; iface< numOfSkinFace; iface++){
                    
                    if(maslave==0)  pSkinFace= pConMesh->getMasterFace(iface);//マスター面
                    if(maslave==1)  pSkinFace= pConMesh->getSlaveFace(iface); //スレーブ面
                    
                    // 自身のMeshに存在するMPC面であれば, RefinしたProgFaceにelemID,elemFaceID,NodeIDをセットする.
                    //
                    if(pSkinFace->isSelf() && pSkinFace->getNumOfEdge()!=0){
                        meshID= pSkinFace->getMeshID(); elemID= pSkinFace->getElementID();
                        pMesh= pAssy->getMesh_ID(meshID);
                        pElem= pMesh->getElement(elemID);
                        
                        pSkinFace->refine(pElem, faceID);//// <<<<<<<<-- 面のRefineと新FaceIDのカウントアップ
                    }else{
                        pSkinFace->refine(NULL, faceID); //// <<<<<<<<-- 面のRefineと新FaceIDのカウントアップ
                    }
                    
                    vProgFace= pSkinFace->getProgFace();//RefineしたSkinFaceを取得
                    
                    if(maslave==0) pProgConMesh->addMasterFace(vProgFace);//progContactMeshにRefineしたSkinFaceを追加セット
                    if(maslave==1) pProgConMesh->addSlaveFace(vProgFace); //  同上
                    
                };//ifaceループ
            };//maslaveループ(マスター,スレーブ切り替え)

//            cout << "MeshFactory::refineContactMesh --- C" << endl;

        };//icontループ(ContactMesh)
    };//ilevelループ(マルチグリッドLevel)



    // 2次要素対応(最終Levelに辺ノード生成)
    // ----
    pAssy =  mpGMGModel->getAssyModel(mMGLevel);//最終レベル
    uiint numOfCont= pAssy->getNumOfContactMesh();
    uiint icont;
    for(icont=0; icont < numOfCont; icont++){
        pConMesh= pAssy->getContactMesh(icont);

        pConMesh->setupAggSkinFace();              //ContactNode周囲のSkinFaceIDを収集
        pConMesh->setupEdgeConNode(NULL, mMGLevel);//辺ノードの生成,辺接続Faceのセット,新ノードをprogConMeshに追加,IDカウント
    };

    // 2次要素対応(最終Levelの辺ConNodeに、メッシュNodeIDをセット)
    // ----
    for(icont=0; icont < numOfCont; icont++){
        pConMesh= pAssy->getContactMesh(icont);
        
        //マスター,スレーブ切り替え
        for(maslave=0; maslave < 2; maslave++){
            uiint numOfFace;
            if(maslave==0) numOfFace = pConMesh->getNumOfMasterFace();
            if(maslave==1) numOfFace = pConMesh->getNumOfSlaveFace();

            uiint iface;
            for(iface=0; iface< numOfFace; iface++){
                if(maslave==0)  pSkinFace= pConMesh->getMasterFace(iface);//マスター面
                if(maslave==1)  pSkinFace= pConMesh->getSlaveFace(iface); //スレーブ面

                // 2次要素
                if(pSkinFace->getOrder()==ElementOrder::Second){
                    //
                    // 自身のMeshに存在するMPC面であれば, NodeIDをセットする.
                    //
                    if(pSkinFace->isSelf() && pSkinFace->getNumOfEdge()!=0){
                        meshID= pSkinFace->getMeshID(); elemID= pSkinFace->getElementID();
                        pMesh= pAssy->getMesh_ID(meshID);
                        pElem= pMesh->getElement(elemID);

                        pSkinFace->setupNodeID_2nd_LastLevel(pElem);//辺ConNodeに、NodeIDをセット
                    }
                }// 2次要素if
                
            };// ifaceループ
        };// マスター スレーブ 切り替え
    };// icont ループ



    // ContactMeshに,八分木を生成 
    //
    for(ilevel=0; ilevel < mMGLevel+1; ilevel++){
        pAssy= mpGMGModel->getAssyModel(ilevel);
        uiint numOfCont= pAssy->getNumOfContactMesh();
        uiint icont;
        for(icont=0; icont < numOfCont; icont++){
            pConMesh= pAssy->getContactMesh(icont);

            uiint nRange;
            nRange= pConMesh->getNumOfConNode();
            
            uiint nDigitCount(0);
            while(nRange > 100){
                nRange /= 10;
                nDigitCount++;
            };
            //maxLayer= ilevel+1; //八分木レイヤー数(八分木テスト用)
            maxLayer= nDigitCount;//八分木レイヤー数
            
            pConMesh->generateOctree(maxLayer);
        };
    };
}



// CommMesh2 (節点共有型 通信テーブル) のRefine
// ----
void CMeshFactory::refineCommMesh2()
{
    CAssyModel *pAssy,*pProgAssy;
    CMesh *pMesh,*pProgMesh;
    CCommMesh2 *pCommMesh2,*pProgCommMesh2;
    CCommFace *pCommFace;
    
    vector<CCommFace*> mvCommFace;
    CCommFace *pProgCommFace;
    uiint countID(0);//progCommFaceのID用(Faceは,新規にIDを割り当てるので,"0"から)
    

    uiint ilevel;
    // progMeshが最上位Levelになるまでループ
    // --
    for(ilevel=0; ilevel< mMGLevel; ilevel++){

        pAssy= mpGMGModel->getAssyModel(ilevel);
        pProgAssy= mpGMGModel->getAssyModel(ilevel+1);
        
        uiint imesh, numOfMesh;
        numOfMesh= pAssy->getNumOfMesh();

        for(imesh=0; imesh< numOfMesh; imesh++){

            pMesh= pAssy->getMesh(imesh);
            pProgMesh= pProgAssy->getMesh(imesh);
            
            uiint icomm, numOfComm;
            numOfComm= pMesh->getCommMesh2Size();

            for(icomm=0; icomm< numOfComm; icomm++){
                pCommMesh2= pMesh->getCommMesh2IX(icomm);
                
                pProgCommMesh2 = new CCommMesh2;//// <<<<<<<<< CommMesh2生成

                pProgCommMesh2->setLevel(ilevel+1);
                pProgCommMesh2->setID(pCommMesh2->getID());
                pProgCommMesh2->setRank(pCommMesh2->getRank());
                pProgCommMesh2->setTransmitRank(pCommMesh2->getTrasmitRank());
                
                pProgMesh->setCommMesh2(pProgCommMesh2);////<<<<< 上位のMeshへprogCommMesh2をセット

                //--------------------
                //CommMesh2のRefine準備
                //--------------------
                pCommMesh2->setupCommNode(pProgCommMesh2);//上位へCommNodeをセット
                pCommMesh2->setupAggFace();
                pCommMesh2->setupEdgeCommNode(pProgCommMesh2, ilevel);//上位へ辺のCommNodeをセット
                pCommMesh2->setupFaceCommNode(pProgCommMesh2);//上位へ面のCommNodeをセット
                
                
                //-----------------
                //CommMesh2のRefine
                //-----------------
                uiint iface, numOfFace;
                numOfFace= pCommMesh2->getCommFaceSize();
                for(iface=0; iface< numOfFace; iface++){
                    pCommFace= pCommMesh2->getCommFaceIX(iface);
                    
                    //CommFaceが載っている要素
                    uiint elemID = pCommFace->getElementID();
                    CElement *pElem= pMesh->getElement(elemID);
                    
                    mvCommFace= pCommFace->refine(pElem);////// <<<<<<<<<<<<<<<<<< リファイン

                    uiint ipface,numOfProgFace;
                    numOfProgFace= mvCommFace.size();
                    for(ipface=0; ipface< numOfProgFace; ipface++){
                        pProgCommFace= mvCommFace[ipface];
                        pProgCommFace->setID(countID);

                        pProgCommMesh2->addCommFace(pProgCommFace);///////// 新CommMesh2へ分割したFaceをセット

                        countID++;/////// 新IDカウントアップ: CommFace
                    };
                };//ifaceループ
                
                
                //-----------------------------
                // CommFace:Quad,Triangleの場合
                //-----------------------------
                // 1.面ノード：Faceの要素ID-Entity番号をたどって,
                //    MeshのNodeをセット
                //----
                // 2.辺ノード：Faceの要素IDと
                //     辺両端のノード情報から,MeshのNodeをセット
                //----
                uiint elemID,entity_num;
                CElement  *pElem;
                CNode     *pFaceNode;
                CCommNode *pFaceCommNode;
                for(iface=0; iface< numOfFace; iface++){

                    pCommFace= pCommMesh2->getCommFaceIX(iface);
                    
                    // FaceCommNodeへNodeをセット
                    //         Quad,Triangle
                    if(pCommFace->getNumOfEdge() > 2){
                        
                        pFaceCommNode= pCommFace->getFaceCommNode();

                        elemID= pCommFace->getElementID();
                        entity_num= pCommFace->getElementFaceID();

                        pElem= pMesh->getElement(elemID);
                        pFaceNode= pElem->getFaceNode(entity_num);

                        pFaceCommNode->setNode(pFaceNode);

                    }//if(NumOfEdge > 2)

                    // 辺のCommNodeへNodeをセット
                    //
                    PairCommNode pairCommNode;
                    CCommNode *pEdgeCommNode;
                    CNode *pNodeFir, *pNodeSec;
                    CNode *pEdgeNode;
                    uiint iedge, numOfEdge;
                    numOfEdge= pCommFace->getNumOfEdge();
                    
                    for(iedge=0; iedge< numOfEdge; iedge++){
                        pairCommNode= pCommFace->getEdgePairCommNode(iedge);

                        pNodeFir= pairCommNode.first->getNode();
                        pNodeSec= pairCommNode.second->getNode();
                        
                        uiint edgeIndex;
                        edgeIndex= pElem->getEdgeIndex(pNodeFir,pNodeSec);
                        pEdgeNode= pElem->getEdgeInterNode(edgeIndex);

                        pEdgeCommNode= pCommFace->getEdgeCommNode(iedge);
                        pEdgeCommNode->setNode(pEdgeNode);
                        
                    };//iedgeループ

                    //// 2 次要素の辺ノードをmvCommNodeへ移し替え => CommMesh2:setupEdgeCommNode に移動
                    //// pCommFace->replaceEdgeCommNode();
                    
                };//ifaceループ
            };//icomm ループ
        };//imesh ループ
    };// iLevel ループ

    //
    // 2次要素の場合、辺ノードを最終Levelに追加
    // * 1次要素も辺に生成されるが、mvCommNodeには追加されない
    //
    pAssy= mpGMGModel->getAssyModel(mMGLevel);//最終LevelのAssyModel
    uiint imesh;
    uiint nNumOfMesh= pAssy->getNumOfMesh();
    for(imesh=0; imesh < nNumOfMesh; imesh++){
        pMesh= pAssy->getMesh(imesh);

        uiint icomm;
        uiint nNumOfComm= pMesh->getCommMesh2Size();

        for(icomm=0; icomm< nNumOfComm; icomm++){
            pCommMesh2= pMesh->getCommMesh2IX(icomm);

            pCommMesh2->setupAggFace();
            pCommMesh2->setupEdgeCommNode(NULL, ilevel);//上位へ辺のCommNodeをセット

            // ---
            // 辺CommNodeへNodeをセット
            // ---
            // Face
            uiint iface;
            uiint nNumOfFace = pCommMesh2->getCommFaceSize();
            for(iface=0; iface< nNumOfFace; iface++){
                pCommFace= pCommMesh2->getCommFaceIX(iface);

                //CommFaceが載っている要素
                uiint elemID = pCommFace->getElementID();
                CElement *pElem= pMesh->getElement(elemID);

                // 辺のCommNodeへNodeをセット
                //
                PairCommNode pairCommNode;
                CCommNode *pEdgeCommNode;
                CNode *pNodeFir, *pNodeSec;
                CNode *pEdgeNode;
                uiint iedge, numOfEdge;
                numOfEdge= pCommFace->getNumOfEdge();

                // Edge
                for(iedge=0; iedge< numOfEdge; iedge++){
                    pairCommNode= pCommFace->getEdgePairCommNode(iedge);

                    pNodeFir= pairCommNode.first->getNode();
                    pNodeSec= pairCommNode.second->getNode();

                    uiint edgeIndex;
                    edgeIndex= pElem->getEdgeIndex(pNodeFir,pNodeSec);
                    pEdgeNode= pElem->getEdgeInterNode(edgeIndex);

                    pEdgeCommNode= pCommFace->getEdgeCommNode(iedge);
                    pEdgeCommNode->setNode(pEdgeNode);

                };//iedge loop
            };//iface loop

        };//icomm loop
        
    };//imesh loop
}






// CommMesh2の生成
// --
void CMeshFactory::GeneCommMesh2(const uiint& mgLevel, const uiint& mesh_id, const uiint& comID,
        const uiint& numOfFace, const uiint& numOfCommNode,
        const uiint& myRank, const uiint& nTransmitRank)
{
    mpTAssyModel= mpGMGModel->getAssyModel(mgLevel);
    mpTMesh= mpTAssyModel->getMesh(mesh_id);

    mpTCommMesh2= new CCommMesh2;// CommMesh2の生成

    mpTCommMesh2->setLevel(mgLevel);
    mpTCommMesh2->setID(comID);
    mpTCommMesh2->reserveCommFace(numOfFace);
    mpTCommMesh2->reserveCommNode(numOfCommNode);
    mpTCommMesh2->setRank(myRank);
    mpTCommMesh2->setTransmitRank(nTransmitRank);

    mpTMesh->setCommMesh2(mpTCommMesh2);
}

// CommMesh2用途のCommFace生成
// --
void CMeshFactory::GeneCommFace(const uiint& mgLevel, const uiint& commeshID, const uiint& face_id,
            const uiint& mesh_id,const uiint elem_id, const uiint& elem_ent_num, const uiint& elem_type, const vuint& vCommNodeID)
{
    mpTAssyModel= mpGMGModel->getAssyModel(mgLevel);
    mpTMesh= mpTAssyModel->getMesh_ID(mesh_id);

    //要素へのマーキング
    CElement *pElement= mpTMesh->getElement(elem_id);
    pElement->markingCommMesh2();
    pElement->markingCommEntity(elem_ent_num);


    mpTCommMesh2= mpTMesh->getCommMesh2(commeshID);

    CCommFace *pCommFace= new CCommFace;// CommFaceの生成

    pCommFace->setID(face_id);
    pCommFace->setElementID(elem_id);
    pCommFace->setElementFaceID(elem_ent_num);
    pCommFace->setMGLevel(mgLevel);
    
    uiint nNumOfVert, nNumOfEdge, nOrder;

    switch(elem_type)
    {
        case(ElementType::Quad):
            nNumOfVert= 4; 
            nNumOfEdge= 4;
            nOrder = ElementOrder::First;
            break;

        case(ElementType::Quad2):
            nNumOfVert= 4;
            nNumOfEdge= 4;
            nOrder = ElementOrder::Second;
            break;

        case(ElementType::Triangle):
            nNumOfVert= 3;
            nNumOfEdge= 3;
            nOrder = ElementOrder::First;
            break;

        case(ElementType::Triangle2):
            nNumOfVert= 3;
            nNumOfEdge= 3;
            nOrder = ElementOrder::Second;
            break;

        case(ElementType::Beam):
            nNumOfVert= 2;
            nNumOfEdge= 1;
            nOrder = ElementOrder::First;
            break;

        case(ElementType::Beam2):
            nNumOfVert= 2;
            nNumOfEdge= 1;
            nOrder = ElementOrder::Second;
            break;
            
        default:
            break;
    }
    pCommFace->initialize(nNumOfVert, nNumOfEdge, nOrder);// CommFace初期化


    uiint nNumOfNode = vCommNodeID.size();
    uiint i, id;
    CCommNode *pCommNode;
    for(i=0; i< nNumOfNode; i++){
        id= vCommNodeID[i];
        pCommNode= mpTCommMesh2->getCommNode(id);

        pCommFace->setCommNode(i, pCommNode);
    };


    bool bSecond(false);
    if(elem_type==ElementType::Quad2)     bSecond=true;
    if(elem_type==ElementType::Triangle2) bSecond=true;
    if(elem_type==ElementType::Beam2)     bSecond=true;

    if(bSecond){
        for(uiint iedge=0; iedge< nNumOfEdge; iedge++){
            id= vCommNodeID[nNumOfVert + iedge];
            pCommNode= mpTCommMesh2->getCommNode(id);
            pCommFace->setEdgeCommNode(pCommNode, iedge);
        };
    }
    mpTCommMesh2->addCommFace(pCommFace);
}

// CommMesh2用途のCommNode生成
// --
void CMeshFactory::GeneCommNodeCM2(const uiint& mgLevel, const uiint& mesh_id, const uiint& node_id,const uiint& commeshID,
        const uiint& comm_node_id, const vdouble& vCoord)
{
    mpTAssyModel= mpGMGModel->getAssyModel(mgLevel);
    mpTMesh= mpTAssyModel->getMesh_ID(mesh_id);
    mpTCommMesh2= mpTMesh->getCommMesh2(commeshID);
    CNode* pNode= mpTMesh->getNode(node_id);

    CCommNode *pCommNode= new CCommNode;// CommNodeの生成

    pCommNode->setID(comm_node_id);
    pCommNode->setNode(pNode);
    pCommNode->setCoord(vCoord);

    mpTCommMesh2->addCommNode(pCommNode);
}


// --
// グループ
// --
//
// ・GroupObjectの生成
// ・GroupID, GroupNameのセット
//
void CMeshFactory::GeneElemGrpOBJ(const uiint& mgLevel, const uiint& mesh_id, const vuint& vGrpID, vstring& vGrpName)//ElementGroupの生成
{
    CAssyModel *pAssyModel = mpGMGModel->getAssyModel(mgLevel);
    CMesh *pMesh = pAssyModel->getMesh_ID(mesh_id);

    uiint i, nNumOfElemGrp = vGrpID.size();
    for(i=0; i < nNumOfElemGrp; i++){

        CElementGroup *pElemGrp = new CElementGroup;

        pElemGrp->setMesh(pMesh);/// Mesh

        uiint nGrpID = vGrpID[i];
        pElemGrp->setID(nGrpID);/// ID

        string sGrpName = vGrpName[i];
        pElemGrp->setName(sGrpName);/// Name

        pMesh->addElemGrp(pElemGrp);
    }
}
//
// ・指定GrpIDへパラメーターをセット
//
void CMeshFactory::setElemID_with_ElemGrp(const uiint& mgLevel, const uiint& mesh_id, const uiint& nGrpID, const vuint& vElemID)
{
    CAssyModel *pAssyModel = mpGMGModel->getAssyModel(mgLevel);
    CMesh *pMesh = pAssyModel->getMesh_ID(mesh_id);

    CElementGroup *pElemGrp = pMesh->getElemGrpID(nGrpID);

    uiint i, nNumOfElem=vElemID.size();

    for(i=0; i < nNumOfElem; i++){
        pElemGrp->addElementID(vElemID[i]);
    }
}

//////
////// Resデータ
//////
////void CMeshFactory::setNodeValue(const uiint& mgLevel, const uiint& nMeshID, const uiint& nNodeID,
////        const uiint& nNumOfSDOF, const uiint& nNumOfVDOF, vdouble& vScaValue, vdouble& vVecValue)
////{
////    CAssyModel *pAssyModel = mpGMGModel->getAssyModel(mgLevel);
////    CMesh *pMesh = pAssyModel->getMesh_ID(nMeshID);
////
////    CNode *pNode = pMesh->getNode(nNodeID);
////
////    uiint idof;
////    uiint nType = pNode->getType();
////
////    switch(nType){
////        case(NodeType::Scalar):
////            for(idof=0; idof < nNumOfSDOF; idof++) pNode->setScalar(vScaValue[idof], idof);
////            break;
////        case(NodeType::Vector):
////            for(idof=0; idof < nNumOfVDOF; idof++) pNode->setVector(vVecValue[idof], idof);
////            break;
////        case(NodeType::ScalarVector):
////            for(idof=0; idof < nNumOfSDOF; idof++) pNode->setScalar(vScaValue[idof], idof);
////            for(idof=0; idof < nNumOfVDOF; idof++) pNode->setVector(vVecValue[idof], idof);
////            break;
////        default:
////            break;
////    }
////
////}

