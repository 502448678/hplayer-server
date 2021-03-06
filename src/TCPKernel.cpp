#include "TCPKernel.h"
#include "Queue.h"
#include "log.h"
#include "err_str.h"
#include <map>
#include <iostream>
#include <list>
using namespace std;

Queue* FileQueue = 0;
RedisTool * g_Redis = 0;
FILE *m_pFile;
int m_nFileSize;
int m_nPos;
#define RootPath   "/home/hmh/Video/"
TCPKernel::TCPKernel()
{
	m_pTCPNet = new TCPNet(this);
	m_threadpool = ThreadPool::GetInstance();
}
TCPKernel::~TCPKernel()
{
	if(m_pTCPNet)
	{
		delete m_pTCPNet;
		m_pTCPNet = NULL;
	}
}

//=== 协议映射表  ===================================================//

	BEGIN_PROTOCOL_MAP
	PM(_DEF_PROTOCOL_REGISTER_RQ,&TCPKernel::RegisterRq)
	PM(_DEF_PROTOCOL_LOGIN_RQ,&TCPKernel::LoginRq)	
	PM(_DEF_PROTOCOL_UPLOAD_RQ,&TCPKernel::UploadRq)
	PM(_DEF_PROTOCOL_UPLOAD_FILEBLOCK_RQ,&TCPKernel::UploadFileBlockRq)
	PM(_DEF_PROTOCOL_DOWNLOAD_RQ,&TCPKernel::DownloadRq)
	PM(_DEF_PROTOCOL_DOWNLOAD_FILEBLOCK_RS,&TCPKernel::DownloadFileBlockRs)
	PM(_DEF_PROTOCOL_PRESSLIKE_RQ, &TCPKernel::PressLikeRq)
	END_PROTOCOL_MAP

//===================================================================//

bool TCPKernel::Open()
{
	LOG_INFO("%s\n","========== hPlayer Server Begin Running .. =============");
	g_Redis = new RedisTool;

    	m_pFile = NULL;
	m_nFileSize = 0;
    	m_nPos = 0;
//    int j= 1234;
//    for( int i = 0 ; i < 30 ; ++i)
//    {
//        g_VideoPort[i]=j++;
//    }
   	q_Init(&FileQueue);

	pthread_mutex_init(&mutex,NULL);
	//连接数据库
	if(!m_sql.ConnectMySql((char*)"localhost",(char*)"root",(char*)"199874",(char*)"hmh_server"))
	{
		cout <<"MySql connect error.." <<endl;
		LOG_ERROR("%s","MySql connect error..\n");
		return false;
	}
	cout <<"MySql connect success.." <<endl;
	LOG_INFO("%s","MySql connect success..\n");
	//加载线程池
	if(!m_threadpool->InitThreadPool(100,5,100))
	{
		cout <<"ThreadPool init error.." <<endl;
		LOG_ERROR("%s","ThreadPool init error..\n");
		return false;
	}
	cout <<"ThreadPool init success.." <<endl;
	LOG_INFO("%s","ThreadPool init success..\n");
	//连接网络
	if(!m_pTCPNet->InitNetWork())
	{
		cout << "Network init error.."<<endl;
		LOG_ERROR("%s","Network init error..\n");
		return false;
	}
	return true;
}

void TCPKernel::Close()
{
	m_pTCPNet->UnInitNetWork();
	m_threadpool->DestroyThreadPool();
}

void TCPKernel::DealData(int sock,char* szbuf)
{
	cout << "DealData .."<<endl;
//	LOG_INFO("%s","DealData ..\n");
	PackType* pType = (PackType*)szbuf;
	int i=0;
	while(1)
	{

		if(m_ProtocolMapEntries[i].m_nType == *pType)
		{
			cout << "m_ProtocalMapEntries["<<i<<"]" <<endl;
//			LOG_INFO("%s[%d]\n","m_ProtocalMapEntries",i);
			(this->*m_ProtocolMapEntries[i].m_pfun)(sock,szbuf);
			break;
		}
		if(m_ProtocolMapEntries[i].m_nType == 0 && m_ProtocolMapEntries[i].m_pfun == 0)
		{
			//break;
			cout << "not m_nType to deal.."<<endl;	
//			LOG_WARN("%s", "not m_nType to deal..\n");
			return;
		}
		i++;
	}
}

void TCPKernel::RegisterRq(int clientfd,char* szbuf)
{
	cout << "clientfd:"<<clientfd<<" ===> "<<"RegisterRq"<<endl;
	LOG_INFO("%s%d ===> %s\n","clientfd:",clientfd,"RegisterRq");
	STRU_REGISTER_RQ * rq = (STRU_REGISTER_RQ *)szbuf;
	STRU_REGISTER_RS rs;
	rs.m_nType = _DEF_PROTOCOL_REGISTER_RS;

	rs.m_lResult = _register_fail;
	//查数据库
	char szsql[_DEF_SQLLEN] = {0};
	bzero( szsql,sizeof(szsql));

	
	list<string> lstStr;
	//查询数据库中是否有这个人
	sprintf( szsql , "select email from t_UserData where email = '%s';",rq->m_useremail);
	cout << szsql <<endl;
	LOG_INFO("%s\n",szsql);
	m_sql.SelectMySql(szsql , 1 , lstStr );


	//如果没有可以插入
	if(lstStr.size() == 0)  
	{
		
		sprintf(szsql , "insert into t_UserData(email,name,password) values('%s','%s','%s');",rq->m_useremail,rq->m_username , rq->m_szPassword);

		cout << szsql << endl;
		LOG_INFO("%s\n",szsql);
		if(m_sql.UpdateMySql( szsql ))
		{
			
			rs.m_lResult = _register_success;
		}
		else{
			rs.m_lResult = _register_userid_is_exist;

		}
	}
		

	//如果存在
	else{
		rs.m_lResult = _register_userid_is_exist;
		cout << "user to register has already exist.." << endl;
//		LOG_INFO("%s","user to register has already exist..\n");
	}
	

	lstStr.clear();
//	cout << "register result:"<<rs.m_lResult<<endl;
	m_pTCPNet->SendData( clientfd , (char*)&rs , sizeof(rs) );
	Log::get_instance()->flush();
	
}
	
void TCPKernel::LoginRq(int clientfd,char* szbuf)
{

	cout << "clientfd:"<<clientfd<<" ===> "<<"LoginRq"<<endl;	
	LOG_INFO("%s%d ===> %s\n","clientfd:",clientfd,"LoginRq");
	STRU_LOGIN_RQ * rq = (STRU_LOGIN_RQ *)szbuf;
	STRU_LOGIN_RS rs;
	rs.m_nType = _DEF_PROTOCOL_LOGIN_RS;
	

	rs.m_lResult = _login_noexist;
	//查数据库
	char szsql[_DEF_SQLLEN] = {0};
	bzero( szsql,sizeof(szsql));

	list<string> lstStr;
	//判斷 是否存在
	sprintf( szsql , "select password from t_UserData where email = '%s';",rq->m_useremail);
	cout <<szsql<<endl;
	LOG_INFO("%s\n",szsql);


	m_sql.SelectMySql(szsql , 1 , lstStr);
	
	if( lstStr.size() == 0 )
	{
		rs.m_lResult = _login_noexist;
		
		LOG_INFO("%s\n","login user not exist");
	}
	else
	{
		rs.m_lResult = _login_password_err;

		string strPassword = lstStr.front();
		lstStr.pop_front();
		

		if( 0 == strcmp(strPassword.c_str(),rq->m_szPassword))
		{
			lstStr.clear();
			bzero(&szsql,sizeof(szsql));

			//char * id = (char*)q_Pop(pQueue);
			//rs.m_userid = atoi(id);
			//cout << "UserEmail:" << rs.m_useremail;
			rs.m_lResult = _login_success;
			
			//登录成功

			char username_sql[_DEF_SQLLEN] = {0};
			sprintf(username_sql,"select name from t_UserData where email = '%s';",rq->m_useremail);
			cout << username_sql;
			LOG_INFO("%s\n",username_sql);

			m_sql.SelectMySql(username_sql,1,lstStr);
			char* username_rs = (char*)lstStr.front().c_str();
			LOG_INFO("%s%s\n","Login success..  login rs name:",username_rs);
			strcpy(rs.m_username ,(const char*)username_rs);

			char userid_sql[_DEF_SQLLEN] = {0};
			sprintf(userid_sql,"select id from t_UserData where email = '%s';",rq->m_useremail);
			cout << userid_sql;
			LOG_INFO("%s\n",userid_sql);

			m_sql.SelectMySql(username_sql,1,lstStr);
			char* userid_rs = (char*)lstStr.front().c_str();
			LOG_INFO("%s%s\n","login rs id:",username_rs);
			rs.m_UserId = atoi(userid_rs);
			// write into redis  用戶id --> 用戶socket ， 用戶名字   id=1 --> 7 , zhangsan  list
			// 超時 5s  心跳 3秒一次發送  超時這個key自動消失
/*			char sztmp[100];
			sprintf(sztmp , "%d",clientfd);
			string strfd = sztmp;

			sprintf( sztmp , "email=%s" , rs.m_useremail);
			string strID = sztmp;

			if( g_Redis->isHashKeyExists(strID) )
			{
				rs.m_lResult = _login_user_online;
			}else
			{
				g_Redis->SetHashValue(  strID,  "fd" , strfd );
				g_Redis->SetHashValue(  strID,  "email" , rq->m_useremail );
				g_Redis->SetHashValue(  strID,  "port" , "0" );
			}
			*/
		}
	}
	lstStr.clear();

//	cout << "login result:"<<rs.m_lResult<<endl;
	m_pTCPNet->SendData( clientfd , (char*)&rs , sizeof(rs) );
	Log::get_instance()->flush();
	
}

/*
void HeartRq(int clientfd ,char* szbuf,int nlen)
{
	//   printf("clientfd:%d HeartRq\n", clientfd);

	STRU_HEART_RQ * rq = (STRU_HEART_RQ*) szbuf;

	char sztmp[100];
	sprintf( sztmp , "id=%d" , rq->m_UserID);

	string strID;
	strID = sztmp;

	g_Redis->SetExpire(strID , 6);
}


*/
void TCPKernel::UploadRq(int clientfd,char* szbuf)
{
	cout <<"clientfd:"<<clientfd<<" UploadRq.."<<endl;
	
	LOG_INFO("clientfd:%d UploadRq..\n",clientfd);
	STRU_UPLOAD_RQ *rq = (STRU_UPLOAD_RQ *)szbuf;
//	FileInfo *info = (FileInfo*)malloc(sizeof(FileInfo));
	FileInfo *info = new FileInfo;
	info->m_nFileID = rq->m_nFileId ;
	info->m_nPos = 0;
	info->m_nFileSize = rq->m_nFileSize;
	memcpy( info->m_szFileName , rq->m_szFileName , _MAX_PATH);
	memcpy( info->m_szFileType , rq->m_szFileType , _MAX_PATH);
	info->m_nUserId = rq->m_UserId;
	memcpy(  info->m_Hobby , rq->m_szHobby , _DEF_HOBBY_COUNT);
	//获取用户名字
	char szsql[_DEF_SQLLEN];
	bzero(szsql,sizeof(szsql));
	Queue* pQueue = NULL;
	q_Init(&pQueue);
	//判断角色是否存在
	cout << "rq->m_UserId:"<<rq->m_UserId<<endl;
	sprintf(szsql,"select name from t_UserData where id = %d;",rq->m_UserId);
	cout <<"uploadRq:" <<szsql << endl;
	if(m_sql.SelectMySql(szsql,1,pQueue) == FALSE)
	{
		err_str("SelectMySql Falied:",-1);
	//	free( info );
		delete info;
		info = NULL;
		q_Destroy(&pQueue);
		return;
	}
	else{
		char * szUserName = (char*)q_Pop(pQueue);
		cout <<"szUserName.." << szUserName <<endl;
		sprintf( info->m_UserName ,"%s",szUserName);
		sprintf( info->m_szFilePath ,"%sflv/%s/%s",RootPath,szUserName,rq->m_szFileName);
	}
	cout <<"info->m_szFilePath:"<<info->m_szFilePath<<endl;
	info->m_VideoID = 0;
	info->pFile = fopen(info->m_szFilePath , "w");
	if( info->pFile )
	{
		q_Push( FileQueue , (void*)info);
	}else{
	free( info );
	}
	q_Destroy(&pQueue);
	Log::get_instance()->flush();
}


char* TCPKernel::GetPicNameOfVideo(char* videoName)
{
	char* picName = (char*)malloc(_MAX_PATH);
	memset(picName,0,_MAX_PATH);
	int i;
	int nlen = strlen(videoName);
	for(i=nlen-1;i>=0;i--)
	{
		if(videoName[i]=='.')
			break;
	}
	memcpy(picName,videoName,i+1);
	strcat(picName,"jpg");
	return picName;
}

void TCPKernel::UploadFileBlockRq(int clientfd,char* szbuf)
{
	cout << "clientfd:" << clientfd<<" UploadFileBlockRq.."<<endl;
	STRU_UPLOAD_FILEBLOCK_RQ *rq = (STRU_UPLOAD_FILEBLOCK_RQ*)szbuf;
	FileInfo*info = 0;
	int64_t nlen = 0;

	Myqueue* tmp = FileQueue->pHead;
	while(tmp)
	{
		info = (FileInfo*) tmp->nValue;
		if(info->m_nUserId == rq->m_nUserId && info->m_nFileID == rq->m_nFileId)
			break;
		tmp = tmp->pNext;
	}
	if(info)
	{
		//写入
		cout << "写入" <<endl;
		nlen = fwrite(rq->m_szFileContent,1,rq->m_nBlockLen,info->pFile);
		info->m_nPos += nlen;
		//文件结束关闭
		if(rq->m_nBlockLen < _MAX_CONTENT_LEN || info->m_nPos >= info->m_nFileSize)
		{
			//关闭文件，删除节点
			cout << "关闭文件，删除节点"<<endl;
			fclose(info->pFile);
			if(strcmp(info->m_szFileType,"jpg")!=0)
			{
				STRU_UPLOAD_RS rs;
				rs.m_nType = _DEF_PROTOCOL_UPLOAD_RS;
				rs.m_nResult = 1;
				//信息写到数据库中
				char szsql[1024];
				bzero(szsql,sizeof(szsql));

				char* picName = GetPicNameOfVideo(info->m_szFileName);
				char* picPath = GetPicNameOfVideo(info->m_szFilePath);
				char rtmp[_RTMP_SIZE] = {0};
				sprintf(rtmp,"//%s/%s",info->m_UserName,info->m_szFileName);
				sprintf(szsql,"insert into t_VideoInfo (userId,videoName,picName,videoPath,picPath,rtmp,food,funny,ennegy,dance,music,video,outside,edu,hotdegree) values(%d,'%s','%s','%s','%s','%s',%d,%d,%d,%d,%d,%d,%d,%d,0);"
                    ,info->m_nUserId , info->m_szFileName , picName ,info->m_szFilePath , picPath , rtmp , info->m_Hobby[0],info->m_Hobby[1],info->m_Hobby[2],info->m_Hobby[3],info->m_Hobby[4],info->m_Hobby[5],info->m_Hobby[6],info->m_Hobby[7]);
				cout <<szsql<<endl;
				if(m_sql.UpdateMySql(szsql) == FALSE)
				{
					err_str("Update MySql Failed..",-1);
				}
				free(picName);
				free(picPath);
				m_pTCPNet->SendData(clientfd,(char*)&rs,sizeof(rs));
			}
			//如果是video 返回一个确认
			q_DeleteNode(FileQueue,(void*)info);
			free(info);
			info = NULL;
		}
	}
}

void TCPKernel::DownloadRq(int clientfd,char*szbuf)
{
    cout<<"clientfd:"<<clientfd<<" DownloadRq"<<endl;
    STRU_DOWNLOAD_RQ *rq = (STRU_DOWNLOAD_RQ*)szbuf;

    cout <<"m_nUserId:"<<rq->m_nUserId<<endl;
    Queue* pList = NULL;
	q_Init(&pList);

    GetDownloadList(  pList,  rq->m_nUserId);

    while(q_GetNum(pList) != 0)
    {
        FileInfo * pInfo = (FileInfo*)q_Pop(pList);
        STRU_DOWNLOAD_RS rs;
        rs.m_nFileId = pInfo->m_nFileID;
        rs.m_nFileSize = pInfo->m_nFileSize;
        rs.m_nType = _DEF_PROTOCOL_DOWNLOAD_RS;
        rs.m_nVideoId = pInfo->m_VideoID;
        strcpy( rs.m_rtmp ,  pInfo->m_szRtmp);
        strcpy( rs.m_szFileName , pInfo->m_szFileName);
        m_pTCPNet->SendData(clientfd,(char*)&rs,sizeof(rs));

        pInfo->pFile = fopen(pInfo->m_szFilePath , "r");
        cout<<pInfo->m_szFilePath<<endl;
        if( pInfo->pFile )
        {
            while(1)
            {
                //文件内容 内容长度 文件Id,文件位置--传输请求
                STRU_DOWNLOAD_FILEBLOCK_RQ blockrq;
                blockrq.m_nType = _DEF_PROTOCOL_DOWNLOAD_FILEBLOCK_RQ;
                int64_t nRelReadNum = (int64_t)fread(blockrq.m_szFileContent,1,_MAX_CONTENT_LEN,pInfo->pFile);

                blockrq.m_nBlockLen = nRelReadNum;
                blockrq.m_nFileId = pInfo->m_nFileID;
                blockrq.m_nUserId = pInfo->m_nUserId;

                cout<<"nRealReadNum:"<<blockrq.m_nBlockLen<<endl;
                m_pTCPNet->SendData(clientfd,(char*)&blockrq,sizeof(blockrq));

                pInfo->m_nPos += nRelReadNum;

                if(pInfo->m_nPos == pInfo->m_nFileSize)
                {
                    fclose(pInfo->pFile);
                    free( pInfo );
                    break;
                }
            }
        }
    }
    q_Destroy(&pList);

}

void TCPKernel::GetDownloadList( Queue*  plist,  int userId)
{
  //  根据id获取喜好， 推荐生成列表播放  /id -->

    char szsql[_DEF_SQLLEN];
	bzero(szsql,sizeof(szsql));
	Queue* pQueue = NULL;
	q_Init(&pQueue);
	char * szbuf = 0;
	int nCount = 0 ;

	//判断角色是否存在
	sprintf(szsql,"select count(videoId) from t_VideoInfo where t_VideoInfo.videoId not in ( select t_UserRecv.videoId from t_UserRecv where t_UserRecv.UserId = %d );",userId);
	cout << szsql <<endl;
	if(m_sql.SelectMySql(szsql,1,pQueue) == FALSE)
	{
		err_str("SelectMySql Falied:",-1);
		q_Destroy(&pQueue);
		return;
	}
    else{
		szbuf = (char*)q_Pop(pQueue);
        nCount = atoi(szbuf);
    }
    if( nCount < 10 )
    {
        bzero(szsql,sizeof(szsql));
        sprintf(szsql,"delete from t_UserRecv  where  userId = %d ;",userId);
        cout << szsql <<endl;
        if(m_sql.UpdateMySql(szsql) == FALSE)
        {
            err_str("Update MySql Failed...",-1);
        }
    }
    bzero(szsql,sizeof(szsql));
    sprintf(szsql,"select videoId , picName , picPath , rtmp from t_VideoInfo where t_VideoInfo.videoId not in ( select t_UserRecv.videoId from t_UserRecv where t_UserRecv.userId = %d );",userId);
    cout << szsql <<endl;
	if(m_sql.SelectMySql(szsql,4,pQueue) == FALSE)
	{
		err_str("SelectMySql Falied:",-1);
		q_Destroy(&pQueue);
		return;
	}
    else{
         //热度推荐
        for( int i = 1 ; i <= 10 ; i++ )
        {
            FileInfo * pInfo = (FileInfo *)malloc(sizeof(FileInfo )) ;
            pInfo->m_nPos = 0;
            pInfo->m_nFileID = i;

            pInfo->m_nUserId = userId;
            szbuf = (char*)q_Pop(pQueue);
            nCount = atoi(szbuf);
            pInfo->m_VideoID = nCount;
            szbuf = (char*)q_Pop(pQueue);
            strcpy( pInfo->m_szFileName ,szbuf);
            szbuf = (char*)q_Pop(pQueue);
            strcpy( pInfo->m_szFilePath, szbuf);
            szbuf = (char*)q_Pop(pQueue);
            strcpy( pInfo->m_szRtmp , szbuf);
            FILE* pFile = fopen(pInfo->m_szFilePath , "r");
            fseek(pFile,0 , SEEK_END);
            pInfo->m_nFileSize =  ftell(pFile);
            fseek(pFile , 0 , SEEK_SET);
            fclose(pFile);
            pInfo->pFile = 0;
            q_Push(plist,(void*)pInfo);
            bzero(szsql,sizeof(szsql));
            sprintf(szsql,"insert into t_userrecv values(%d ,%d);",userId,pInfo->m_VideoID );
            cout << szsql <<endl;
            if(m_sql.UpdateMySql(szsql) == FALSE)
            {
                err_str("Update MySql Failed...",-1);
            }
        }
    }




//
//    pInfo = (FileInfo *)malloc(sizeof(FileInfo )) ;
//    pInfo->m_nPos = 0;
//    pInfo->m_nFileID = 2;
//    pInfo->m_nFileSize = 13724;
//    pInfo->m_nUserId = userId;
//    strcpy( pInfo->m_szFileName ,"2.jpg");
//    strcpy( pInfo->m_szFilePath, "/home/colin/Video/img/2.jpg");
//    strcpy( pInfo->m_szRtmp , "2.flv");
//    pInfo->pFile = 0;
//    q_Push(plist,(void*)pInfo);
//
//    pInfo = (FileInfo *)malloc(sizeof(FileInfo )) ;
//    pInfo->m_nPos = 0;
//    pInfo->m_nFileID = 3;
//    pInfo->m_nFileSize = 27430;
//    pInfo->m_nUserId = userId;
//    strcpy( pInfo->m_szFileName ,"3.jpg");
//    strcpy( pInfo->m_szFilePath, "/home/colin/Video/img/3.jpg");
//    strcpy( pInfo->m_szRtmp , "3.flv");
//    pInfo->pFile = 0;
//    q_Push(plist,(void*)pInfo);


}

void TCPKernel::DownloadFileBlockRs(int clientfd,char*szbuf)
{
    STRU_DOWNLOAD_FILEBLOCK_RS *psufr = (STRU_DOWNLOAD_FILEBLOCK_RS *)szbuf;

    Myqueue * tmp = FileQueue->pHead;
    FileInfo * pInfo = 0;
    while(tmp)
    {
        if( ((FileInfo*)tmp ->nValue)->m_nFileID == psufr->m_nFileId
        && ((FileInfo*)tmp ->nValue)->m_nUserId ==  psufr->m_nUserId )
        {
            pInfo = (FileInfo*)tmp ->nValue;
            break;
        }
        tmp = tmp->pNext;
    }
    if( !pInfo ) return;

	STRU_DOWNLOAD_FILEBLOCK_RQ sufr;
	sufr.m_nType = _DEF_PROTOCOL_DOWNLOAD_FILEBLOCK_RQ;
	sufr.m_nFileId = pInfo->m_nFileID;
	sufr.m_nUserId = pInfo->m_nUserId;
	//如果文件块客户端接收失败，再次发送
	if(psufr->m_nResult == _downloadfileblock_fail)
	{
		//移动文件指针pInfo->m_nDownLoadSize
		fseeko64(pInfo->pFile,pInfo->m_nPos,SEEK_SET);
		int64_t nRelReadNum = (int64_t)fread(sufr.m_szFileContent,1,_MAX_CONTENT_LEN,pInfo->pFile);
		sufr.m_nBlockLen = nRelReadNum;
		//读文件内容并发送
		m_pTCPNet->SendData(clientfd,(char*)&sufr,sizeof(sufr));
		return;
	}
	pInfo->m_nPos += psufr->m_nBlockLen;

	if(pInfo->m_nPos == pInfo->m_nFileSize)
	{
		fclose(pInfo->pFile);

		Myqueue * tmp = FileQueue->pHead;
		if( ((FileInfo*)tmp->nValue) == pInfo )
		{
            tmp =(Myqueue*) q_Pop( FileQueue );
		}else{
            while(tmp)
            {
                if(tmp->pNext && ((FileInfo*)tmp->pNext->nValue)  == pInfo )
                {
                    tmp = tmp->pNext->pNext;
                    break;
                }
                tmp = tmp->pNext;
            }
		}
        free( pInfo );
		return;
	}
	else
	{
		 	int64_t nRelReadNum = (int64_t)fread(sufr.m_szFileContent,1,_MAX_CONTENT_LEN,pInfo->pFile);
			sufr.m_nBlockLen = nRelReadNum;
			printf("m_nBlockLen:%d\n",sufr.m_nBlockLen);
	}
	m_pTCPNet->SendData(clientfd,(char*)&sufr,sizeof(sufr));

}

void TCPKernel::PressLikeRq(int clientfd,char*szbuf)
{
    STRU_PRESSLIKE_RQ * rq = (STRU_PRESSLIKE_RQ *)szbuf;

    char szsql[_DEF_SQLLEN];
	bzero(szsql,sizeof(szsql));

	sprintf(szsql,"insert into t_useraction values( %d,%d);",rq->m_nUserId , rq->m_nVideoId);
    cout << szsql <<endl;
    if(m_sql.UpdateMySql(szsql) == FALSE)
    {
        err_str("Update MySql Failed...",-1);
        return;
    }
    //更新视频热度
    bzero(szsql,sizeof(szsql));
    sprintf(szsql,"update t_VideoInfo set hotdegree = hotdegree +1 where videoId =%d;" , rq->m_nVideoId);
    cout << szsql <<endl;
    if(m_sql.UpdateMySql(szsql) == FALSE)
    {
        err_str("Update MySql Failed...",-1);
        return;
    }
    //取出影片类型 取出用户喜好， 给用户喜好加分
    bzero(szsql,sizeof(szsql));
    Queue* pQueue = NULL;
	q_Init(&pQueue);
	char * szChar = 0;

	int food , funny ,ennegy ,dance , music,  video,  outside , edu ;
	int food1 , funny1 ,ennegy1 ,dance1 , music1,  video1,  outside1 , edu1;

	sprintf(szsql,"select food , funny ,ennegy ,dance , music,  video,  outside , edu from t_VideoInfo where videoId = %d;", rq->m_nVideoId);
    cout << szsql <<endl;
    if(m_sql.SelectMySql(szsql,8,pQueue) == FALSE)
    {
        err_str("SelectMysql Failed...",-1);
        q_Destroy(&pQueue);
        return;
    }else{
        szChar = (char*)q_Pop(pQueue);
        food = atoi(szChar);
        szChar = (char*)q_Pop(pQueue);
        funny = atoi(szChar);
        szChar = (char*)q_Pop(pQueue);
        ennegy = atoi(szChar);
        szChar = (char*)q_Pop(pQueue);
        dance = atoi(szChar);
        szChar = (char*)q_Pop(pQueue);
        music = atoi(szChar);
        szChar = (char*)q_Pop(pQueue);
        video = atoi(szChar);
        szChar = (char*)q_Pop(pQueue);
        outside = atoi(szChar);
        szChar = (char*)q_Pop(pQueue);
        edu = atoi(szChar);
    }
    bzero(szsql,sizeof(szsql));
    sprintf(szsql,"select food , funny ,ennegy ,dance , music,  video,  outside , edu from t_UserData where id = %d;", rq->m_nUserId);
    cout <<szsql<<endl;
    if(m_sql.SelectMySql(szsql,8,pQueue) == FALSE)
    {
        err_str("SelectMysql Failed...",-1);
        q_Destroy(&pQueue);
        return;
    }else{
        szChar = (char*)q_Pop(pQueue);
        food1 = atoi(szChar);
        szChar = (char*)q_Pop(pQueue);
        funny1 = atoi(szChar);
        szChar = (char*)q_Pop(pQueue);
        ennegy1 = atoi(szChar);
        szChar = (char*)q_Pop(pQueue);
        dance1 = atoi(szChar);
        szChar = (char*)q_Pop(pQueue);
        music1 = atoi(szChar);
        szChar = (char*)q_Pop(pQueue);
        video1 = atoi(szChar);
        szChar = (char*)q_Pop(pQueue);
        outside1 = atoi(szChar);
        szChar = (char*)q_Pop(pQueue);
        edu1 = atoi(szChar);
    }
        food   += food1;
        funny  += funny1;
        ennegy += ennegy1;
        dance  += dance1;
        music  += music1;
        video  += video1;
        outside += outside1;
        edu    += edu1;

        bzero(szsql,sizeof(szsql));
        sprintf(szsql,"update t_UserData set food =%d , funny =%d,ennegy =%d,dance=%d , music=%d,  video=%d,  outside=%d , edu=%d where id = %d;"
        ,food,funny,ennegy , dance,music , video , outside , edu , rq->m_nUserId);
        cout << szsql <<endl;
        if(m_sql.UpdateMySql(szsql) == FALSE)
        {
            err_str("Update MySql Failed...",-1);
        }
}


