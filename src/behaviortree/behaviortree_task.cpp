﻿/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Tencent is pleased to support the open source community by making behaviac available.
//
// Copyright (C) 2015 THL A29 Limited, a Tencent company. All rights reserved.
//
// Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in compliance with
// the License. You may obtain a copy of the License at http://opensource.org/licenses/BSD-3-Clause
//
// Unless required by applicable law or agreed to in writing, software distributed under the License is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and limitations under the License.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "behaviac/behaviortree/behaviortree.h"
#include "behaviac/behaviortree/behaviortree_task.h"
#include "behaviac/agent/registermacros.h"

#include "behaviac/behaviortree/attachments/event.h"

#include "behaviac/base/core/profiler/profiler.h"
#include "behaviac/behaviortree/attachments/effector.h"

#include "behaviac/fsm/state.h"
#include "behaviac/behaviortree/attachments/effector.h"
#include "behaviac/fsm/startcondition.h"
#include "behaviac/fsm/transitioncondition.h"


#if BEHAVIAC_COMPILER_MSVC
#include <windows.h>
#endif//BEHAVIAC_COMPILER_MSVC

BEGIN_ENUM_DESCRIPTION(behaviac::EBTStatus, EBTStatus)
{
    ENUMCLASS_DISPLAYNAME(L"BT状态");
    ENUMCLASS_DESC(L"BT状态");

    DEFINE_ENUM_VALUE(behaviac::BT_INVALID, "invalid");
    DEFINE_ENUM_VALUE(behaviac::BT_SUCCESS, "success");
    DEFINE_ENUM_VALUE(behaviac::BT_FAILURE, "failure");
    DEFINE_ENUM_VALUE(behaviac::BT_RUNNING, "running");
}
END_ENUM_DESCRIPTION()

namespace behaviac
{
	BehaviorTask::BehaviorTask() : m_status(BT_INVALID), m_node(0), m_parent(0), m_attachments(0), m_id((uint16_t)-1), m_bHasManagingParent(false)
    {
    }

    BehaviorTask::~BehaviorTask()
    {
        this->FreeAttachments();
    }

    void BehaviorTask::FreeAttachments()
    {
        if (this->m_attachments)
        {
            for (size_t i = 0; i < this->m_attachments->size(); ++i)
            {
                BehaviorTask* pAttachment = (*m_attachments)[i];
                BEHAVIAC_DELETE(pAttachment);
            }

            this->m_attachments->clear();
            BEHAVIAC_DELETE(this->m_attachments);
            this->m_attachments = 0;
        }
    }

    void BehaviorTask::Clear()
    {
        this->m_status = BT_INVALID;
        this->m_parent = 0;
		this->m_id = (uint16_t)-1;
        this->FreeAttachments();

        this->m_node = 0;
    }

    void BehaviorTask::Init(const BehaviorNode* node)
    {
        BEHAVIAC_ASSERT(node);

        this->m_node = (const BehaviorNode*)node;
        this->m_id = this->m_node->GetId();
    }

    void BehaviorTask::DestroyTask(BehaviorTask* task)
    {
        BEHAVIAC_DELETE(task);
    }

    void BehaviorTask::Attach(AttachmentTask* pAttachment)
    {
        if (!this->m_attachments)
        {
            this->m_attachments = BEHAVIAC_NEW Attachments;
        }

        this->m_attachments->push_back(pAttachment);
    }

    const BehaviorTask* BehaviorTask::GetTaskById(int id) const
    {
        BEHAVIAC_ASSERT(id != -1);

        if (this->m_id == id)
        {
            return this;
        }

        return 0;
    }

	int BehaviorTask::GetNextStateId() const
	{
		return -1;
	}

    const behaviac::string& BehaviorTask::GetClassNameString() const
    {
        if (this->m_node)
        {
            return this->m_node->GetClassNameString();
        }

        static behaviac::string s_subBT("SubBT");
        return s_subBT;
    }

	uint16_t BehaviorTask::GetId() const
    {
        return this->m_id;
    }

	void BehaviorTask::SetId(uint16_t id)
    {
        this->m_id = id;
    }

    const BehaviorNode* BehaviorTask::GetNode() const
    {
        return this->m_node;
    }

	void BehaviorTask::onreset(Agent* pAgent)
	{
        BEHAVIAC_UNUSED_VAR(pAgent);
	}

    bool BehaviorTask::onenter(Agent* pAgent)
    {
        BEHAVIAC_UNUSED_VAR(pAgent);
        return true;
    }

    //max number of threads used for BT ticking.
    //usually, only 1 thread is used for BT ticking.
	const static uint32_t kMaxThreads = 32;

    struct ThreadStatus_t
    {
        THREAD_ID_TYPE	tid_;
        EBTStatus		status_;
        int				nodeId_;
    };

    //thread safe and no more mutex or memory allocation
    //
    static behaviac::Mutex* gs_tickingMutex = 0;
    static ThreadStatus_t gs_lastStatus[kMaxThreads];

    static behaviac::Mutex& GetTickingMutex()
    {
        if (!gs_tickingMutex)
        {
            gs_tickingMutex = BEHAVIAC_NEW behaviac::Mutex;
        }

        return *gs_tickingMutex;
    }

    void CleanupTickingMutex()
    {
        BEHAVIAC_DELETE gs_tickingMutex;
        gs_tickingMutex = 0;
    }

    EBTStatus GetNodeExitStatus()
    {
        THREAD_ID_TYPE tid = behaviac::GetTID();

        {
            behaviac::ScopedLock lock(GetTickingMutex());

			for (uint32_t i = 0; i < kMaxThreads; ++i)
            {
                const ThreadStatus_t& ts = gs_lastStatus[i];

                if (ts.tid_ == tid)
                {
                    return ts.status_;
                }
            }
        }

        BEHAVIAC_ASSERT(false, "this function is only valid when it is called instead of an 'ExitAction' of any Node.\n \
							   							   							   							   							   							   							   							   							   							   							   							   							   			it should not be called in any other functions!");

        return BT_INVALID;
    }

	uint32_t SetNodeId(uint32_t nodeId)
    {
        THREAD_ID_TYPE tid = behaviac::GetTID();

		uint32_t slot = (uint32_t)-1;
        {
            behaviac::ScopedLock lock(GetTickingMutex());

			for (uint32_t i = 0; i < kMaxThreads; ++i)
            {
                ThreadStatus_t& ts = gs_lastStatus[i];

                if (ts.tid_ == 0)
                {
                    ts.tid_ = tid;
                    ts.nodeId_ = nodeId;
                    slot = i;

                    break;
                }
            }
        }

        return slot;
    }

    void ClearNodeId(uint32_t slot)
    {
        THREAD_ID_TYPE tid = behaviac::GetTID();

        BEHAVIAC_UNUSED_VAR(tid);
        {
            behaviac::ScopedLock lock(GetTickingMutex());

            ThreadStatus_t& ts = gs_lastStatus[slot];
            BEHAVIAC_ASSERT(ts.tid_ == tid);

            ts.tid_ = 0;
            ts.nodeId_ = INVALID_NODE_ID;
        }
    }

    int GetNodeId()
    {
        THREAD_ID_TYPE tid = behaviac::GetTID();

        {
            behaviac::ScopedLock lock(GetTickingMutex());

			for (uint32_t i = 0; i < kMaxThreads; ++i)
            {
                const ThreadStatus_t& ts = gs_lastStatus[i];

                if (ts.tid_ == tid)
                {
                    return ts.nodeId_;
                }
            }
        }

        //BEHAVIAC_ASSERT(false, "this function is only valid when it is called instead of an 'Action' Node.\n");

        return INVALID_NODE_ID;
    }

    EBTStatus BehaviorTask::update(Agent* pAgent, EBTStatus childStatus)
    {
        BEHAVIAC_UNUSED_VAR(pAgent);
        BEHAVIAC_UNUSED_VAR(childStatus);
        return BT_SUCCESS;
    }

    EBTStatus BehaviorTask::update_current(Agent* pAgent, EBTStatus childStatus)
    {
        EBTStatus s = this->update(pAgent, childStatus);
        return s;
    }

    void BehaviorTask::onexit(Agent* pAgent, EBTStatus status)
    {
        BEHAVIAC_UNUSED_VAR(pAgent);
        BEHAVIAC_UNUSED_VAR(status);
    }

#if !BEHAVIAC_RELEASE
    behaviac::string BehaviorTask::GetTickInfo(const behaviac::Agent* pAgent, const behaviac::BehaviorTask* b, const char* action)
    {
        return BehaviorTask::GetTickInfo(pAgent, b->GetNode(), action);
    }

    behaviac::string BehaviorTask::GetTickInfo(const behaviac::Agent* pAgent, const behaviac::BehaviorNode* n, const char* action)
    {
        if (pAgent && pAgent->IsMasked())
        {
            //BEHAVIAC_PROFILE("GetTickInfo", true);

            const behaviac::string& bClassName = n->GetClassNameString();

            //filter out intermediate bt, whose class name is empty
            if (!bClassName.empty())
            {
                int nodeId = n->GetId();
                const BehaviorTreeTask* bt = pAgent ? pAgent->btgetcurrent() : 0;

                //TestBehaviorGroup\scratch.xml->EventetTask[0]:enter
                behaviac::string bpstr;

                if (bt)
                {
                    const behaviac::string& btName = bt->GetName();

                    bpstr = FormatString("%s.xml->", btName.c_str());
                }

                bpstr += FormatString("%s[%i]", bClassName.c_str(), nodeId);

                if (action)
                {
                    bpstr += FormatString(":%s", action);
                }

                return bpstr;
            }
        }

        return behaviac::string();
    }

#define _MY_BREAKPOINT_BREAK_(pAgent, btMsg, actionResult) \
    { \
        /*const char* actionResultStr = GetActionResultStr(actionResult); \
                                                                			const char* msg = FormatString("BehaviorTreeTask Breakpoints at: %s(%d)\n\n'%s%s'\n\nOk to continue.", __FILE__, __LINE__, btMsg, actionResultStr); \
                                                                			if (IDOK == MessageBoxA(0, msg, "BehaviorTreeTask Breakpoints", MB_OK | MB_ICONHAND | MB_SETFOREGROUND | MB_SYSTEMMODAL)) \
                                                                			{ \
                                                                				DebugBreak_(); \
                                                                			}*/ \
        Workspace::GetInstance()->WaitforContinue(); \
    }

    //CheckBreakpoint should be after log of onenter/onexit/update, as it needs to flush msg to the client
	void CHECK_BREAKPOINT(Agent* pAgent, const BehaviorNode* b, const char* action, EActionResult actionResult)
	{
		if (Config::IsLoggingOrSocketing())
		{
			behaviac::string bpstr = behaviac::BehaviorTask::GetTickInfo(pAgent, b, action);
			if (!bpstr.empty())
			{
				LogManager::GetInstance()->Log(pAgent, bpstr.c_str(), actionResult, ELM_tick);
				if (Workspace::GetInstance()->CheckBreakpoint(pAgent, b, action, actionResult))
				{
					LogManager::GetInstance()->Log(pAgent, bpstr.c_str(), actionResult, ELM_breaked);
					LogManager::GetInstance()->Flush(pAgent);
					behaviac::Socket::Flush();
					BreakpointPromptHandler_fn fn = GetBreakpointPromptHandler();
					if (fn == 0)
					{
						_MY_BREAKPOINT_BREAK_(pAgent, bpstr.c_str(), actionResult);
					}
					else
					{
						fn(bpstr.c_str());
					}
					LogManager::GetInstance()->Log(pAgent, bpstr.c_str(), actionResult, ELM_continue);
					LogManager::GetInstance()->Flush(pAgent);
					behaviac::Socket::Flush();
				}
			}
		}
	}
#else
	void CHECK_BREAKPOINT(Agent* pAgent, const BehaviorNode* b, const char* action, EActionResult actionResult)
	{
        BEHAVIAC_UNUSED_VAR(pAgent);
        BEHAVIAC_UNUSED_VAR(b);
        BEHAVIAC_UNUSED_VAR(action);
        BEHAVIAC_UNUSED_VAR(actionResult);
	}
#endif//#if !BEHAVIAC_RELEASE

    bool BehaviorTask::onenter_action(Agent* pAgent)
    {
        this->m_node->InstantiatePars(pAgent);
        bool bResult = this->CheckPreconditions(pAgent, false);

        if (bResult)
        {
			this->m_bHasManagingParent = false;
			this->SetCurrentTask(0);

            bResult = this->onenter(pAgent);

            if (!bResult)
            {
                return false;
            }
            else
            {
#if !BEHAVIAC_RELEASE
                //BEHAVIAC_PROFILE_DEBUGBLOCK("Debug", true);

                CHECK_BREAKPOINT(pAgent, this->m_node, "enter", bResult ? EAR_success : EAR_failure);
#endif
            }
        }

        return bResult;
    }

    bool BehaviorTask::CheckPreconditions(Agent* pAgent, bool bIsAlive)
    {
        bool bResult = true;

        if (m_node != 0)
        {
            if (m_node->m_preconditions.size() > 0)
            {
                bResult = ((BehaviorNode*)m_node)->CheckPreconditions(pAgent, bIsAlive);
            }
        }

        return bResult;
    }

    void BehaviorTask::onexit_action(Agent* pAgent, EBTStatus status)
    {
        this->onexit(pAgent, status);

        if (this->m_node != 0)
        {
            Effector::EPhase phase = Effector::E_SUCCESS;

            if (status == BT_FAILURE)
            {
                phase = Effector::E_FAILURE;
            }
            else
            {
                BEHAVIAC_ASSERT(status == BT_SUCCESS);
            }

            this->m_node->ApplyEffects(pAgent, (BehaviorNode::EPhase)phase);

            this->m_node->UnInstantiatePars(pAgent);

#if !BEHAVIAC_RELEASE
			if (Config::IsLoggingOrSocketing())
			{
				//BEHAVIAC_PROFILE_DEBUGBLOCK("Debug", true);
				if (status == BT_SUCCESS)
				{
					CHECK_BREAKPOINT(pAgent, this->m_node, "exit", EAR_success);
				}
				else
				{
					CHECK_BREAKPOINT(pAgent, this->m_node, "exit", EAR_failure);
				}
			}
#endif
        }
    }

    /*
    Get the Root of branch task
    */
    BranchTask* BehaviorTask::GetTopManageBranchTask()
    {
        BranchTask* tree = 0;
        BehaviorTask* task = this->m_parent;

        while (task != 0)
        {
            if (BehaviorTreeTask::DynamicCast(task) != 0)
            {
                //to overwrite the child branch
                tree = (BranchTask*)task;
                break;
            }
            else if (task->m_node ? task->m_node->IsManagingChildrenAsSubTrees() : false)
            {
                //until it is Parallel/SelectorLoop, it's child is used as tree to store current task
                break;
            }
            else if (BranchTask::DynamicCast(task) != 0)
            {
                //this if must be after BehaviorTreeTask and IsManagingChildrenAsSubTrees
                tree = (BranchTask*)task;
            }
            else
            {
                BEHAVIAC_ASSERT(false);
            }

            task = task->m_parent;
        }

        return tree;
    }

#if BEHAVIAC_ENABLE_PROFILING
    /// Helper class for automatically beginning and ending a profiling block
    class AutoProfileBlockSend
    {
    public:
        /// Construct. begin a profiling block with the specified name and optional call count.
        AutoProfileBlockSend(Profiler* profiler, const behaviac::string& taskClassid, const Agent* agent) : profiler_(profiler)
        {
            if (Config::IsProfiling())
            {
                profiler_ = profiler;

                if (profiler_)
                {
                    profiler_->BeginBlock(taskClassid.c_str(), agent);
                }
            }
        }

        /// Destruct. end the profiling block.
        ~AutoProfileBlockSend()
        {
            if (Config::IsProfiling())
            {
                if (profiler_)
                {
                    profiler_->EndBlock(true);
                }
            }
        }

    private:
        /// Profiler.
        Profiler* profiler_;
    };
#endif

    EBTStatus BehaviorTask::exec(Agent* pAgent)
    {
        EBTStatus childStatus = BT_RUNNING;
        return this->exec(pAgent, childStatus);
    }

    EBTStatus BehaviorTask::exec(Agent* pAgent, EBTStatus childStatus)
    {
#if BEHAVIAC_ENABLE_PROFILING
#if 1
        const char* classStr = (this->m_node ? this->m_node->GetClassNameString().c_str() : "BT");
        int nodeId = (this->m_node ? this->m_node->GetId() : -1);
        behaviac::string taskClassid = FormatString("%s[%i]", classStr, nodeId);

        AutoProfileBlockSend profiler_block(Profiler::GetInstance(), taskClassid, pAgent);
#else
        const char* classStr = (this->m_node ? this->m_node->GetClassNameString().c_str() : "BT");
        BEHAVIAC_PROFILE(classStr);
#endif
#endif//#if BEHAVIAC_ENABLE_PROFILING

        BEHAVIAC_ASSERT(!this->m_node || this->m_node->IsValid(pAgent, this),
                        FormatString("Agent In BT:%s while the Agent used for: %s", this->m_node->m_agentType.c_str(), pAgent->GetClassTypeName()));

        bool bEnterResult = false;

        if (this->m_status == BT_RUNNING)
        {
            bEnterResult = true;
        }
        else
        {
            //reset it to invalid when it was success/failure
            this->m_status = BT_INVALID;

            bEnterResult = this->onenter_action(pAgent);
        }

        if (bEnterResult)
        {
#if !BEHAVIAC_RELEASE

            if (Config::IsLoggingOrSocketing())
            {
                string btStr = BehaviorTask::GetTickInfo(pAgent, this, "update");

                //empty btStr is for internal BehaviorTreeTask
                if (!StringUtils::IsNullOrEmpty(btStr.c_str()))
                {
                    LogManager::GetInstance()->Log(pAgent, btStr.c_str(), EAR_none, ELM_tick);
                }
            }
#endif
			bool bValid = this->CheckParentUpdatePreconditions(pAgent);

            if (bValid)
            {
                this->m_status = this->update_current(pAgent, childStatus);
            }
            else
            {
                this->m_status = BT_FAILURE;

				if (this->GetCurrentTask())
				{
					this->update_current(pAgent, BT_FAILURE);
				}
            }

            if (this->m_status != BT_RUNNING)
            {
                //clear it

                this->onexit_action(pAgent, this->m_status);

                //this node is possibly ticked by its parent or by the topBranch who records it as currrent node
                //so, we can't here reset the topBranch's current node
            }
            else
            {
                BranchTask* tree = this->GetTopManageBranchTask();

                if (tree != 0)
                {
                    tree->SetCurrentTask(this);
                }
            }
        }
        else
        {
            this->m_status = BT_FAILURE;
        }

        return this->m_status;
    }

	bool BehaviorTask::CheckParentUpdatePreconditions(Agent* pAgent)
	{
		bool bValid = true;

		if (this->m_bHasManagingParent)
		{
			bool bHasManagingParent = false;
			const int kMaxParentsCount = 512;
			int parentsCount = 0;
			BehaviorTask* parents[kMaxParentsCount];

			BranchTask* parentBranch = this->GetParent();

			parents[parentsCount++] = this;

			//back track the parents until the managing branch
			while (parentBranch != 0)
			{
				BEHAVIAC_ASSERT(parentsCount < kMaxParentsCount, "weird tree!");

				parents[parentsCount++] = parentBranch;

				if (parentBranch->GetCurrentTask() == this)
				{
					//BEHAVIAC_ASSERT(parentBranch->GetNode()->IsManagingChildrenAsSubTrees());

					bHasManagingParent = true;
					break;
				}

				parentBranch = parentBranch->GetParent();
			}

			if (bHasManagingParent)
			{
				for (int i = parentsCount - 1; i >= 0; --i)
				{
					BehaviorTask* pb = parents[i];

					bValid = pb->CheckPreconditions(pAgent, true);

					if (!bValid)
					{
						break;
					}
				}
			}
		}
		else
		{
			bValid = this->CheckPreconditions(pAgent, true);
		}

		return bValid;
	}

    bool abort_handler(BehaviorTask* node, Agent* pAgent, void* user_data)
    {
        BEHAVIAC_UNUSED_VAR(user_data);

        if (node->m_status == BT_RUNNING)
        {
            node->onexit_action(pAgent, BT_FAILURE);

            node->m_status = BT_FAILURE;

            node->SetCurrentTask(0);
        }

        return true;
    }

    bool reset_handler(BehaviorTask* node, Agent* pAgent, void* user_data)
    {
        BEHAVIAC_UNUSED_VAR(user_data);
        BEHAVIAC_UNUSED_VAR(pAgent);

        node->m_status = BT_INVALID;

        node->SetCurrentTask(0);

        node->onreset(pAgent);

        return true;
    }

    void BehaviorTask::abort(Agent* pAgent)
    {
        this->traverse(&abort_handler, pAgent, 0);
    }

    void BehaviorTask::reset(Agent* pAgent)
    {
#if BEHAVIAC_ENABLE_PROFILING
        BEHAVIAC_PROFILE("BehaviorTask::reset");
#endif
        this->traverse(&reset_handler, pAgent, 0);
    }

    AttachmentTask::AttachmentTask() : BehaviorTask()
    {}

    void AttachmentTask::Init(const BehaviorNode* node)
    {
        super::Init(node);
    }

    AttachmentTask::~AttachmentTask()
    {}

    void AttachmentTask::traverse(NodeHandler_t handler, Agent* pAgent, void* user_data)
    {
        handler(this, pAgent, user_data);
    }

    BranchTask::BranchTask() : BehaviorTask(), m_currentNodeId(-1), m_currentTask(0)
    {
    }

    int  BranchTask::GetCurrentNodeId()
    {
        return this->m_currentNodeId;
    }

    void BranchTask::SetCurrentNodeId(int id)
    {
        this->m_currentNodeId = id;
    }

    BranchTask::~BranchTask()
    {
    }

    bool BranchTask::onenter(Agent* pAgent)
    {
        BEHAVIAC_UNUSED_VAR(pAgent);
        //return super::onenter(pAgent);
        return true;
    }

    void BranchTask::onexit(Agent* pAgent, EBTStatus s)
    {
        BEHAVIAC_UNUSED_VAR(pAgent);
        BEHAVIAC_UNUSED_VAR(s);
        //super::onexit(pAgent, s);
    }

    //
    //Set the m_currentTask as task
    //if the leaf node is runninng ,then we should set the leaf's parent node also as running
    //
    void BranchTask::SetCurrentTask(BehaviorTask* task)
    {
        if (task != 0)
        {
            //if the leaf node is running, then the leaf's parent node is also as running,
            //the leaf is set as the tree's current task instead of its parent
            if (this->m_currentTask == 0)
            {
                BEHAVIAC_ASSERT(this->m_currentTask != this);
                this->m_currentTask = task;
				task->SetHasManagingParent(true);
            }
        }
        else
        {
			if (this->m_status != BT_RUNNING)
			{
                this->m_currentTask = task;
			}
        }
    }

    EBTStatus BehaviorTask::GetStatus() const
    {
        return this->m_status;
    }

    // static bool getBooleanFromStatus(EBTStatus status)
    // {
    //     if (status == BT_FAILURE) {
    //         return false;

    //     } else if (status == BT_SUCCESS) {
    //         return true;

    //     } else {
    //         BEHAVIAC_ASSERT(false);
    //         return false;
    //     }
    // }

    bool BehaviorTask::CheckEvents(const char* eventName, Agent* pAgent) const
    {
        return this->m_node->CheckEvents(eventName, pAgent);
    }

    void BehaviorTask::copyto(BehaviorTask* target) const
    {
        target->m_status = this->m_status;
    }

    void BehaviorTask::save(ISerializableNode* node) const
    {
        if (this->m_status != BT_INVALID)
        {
            CSerializationID  classId("class");
            node->setAttr(classId, this->GetClassNameString());

            CSerializationID  idId("id");
            node->setAttr(idId, this->GetId());

            CSerializationID  statusId("status");
            node->setAttr(statusId, this->m_status);
        }
    }

    void BehaviorTask::load(ISerializableNode* node)
    {
        CSerializationID  attrId("status");
        behaviac::string attrStr;

        if (node->getAttr(attrId, attrStr))
        {
            behaviac::StringUtils::FromString(attrStr.c_str(), this->m_status);
        }

#if !BEHAVIAC_RELEASE

        if (this->m_status != BT_INVALID)
        {
            CSerializationID  classId("class");
            node->getAttr(classId, attrStr);
            BEHAVIAC_ASSERT(attrStr == this->GetClassNameString());

            CSerializationID  idId("id");
            node->getAttr(idId, attrStr);
            int id = -1;
            StringUtils::FromString(attrStr.c_str(), id);
            BEHAVIAC_ASSERT(id == this->GetId());
        }

#endif
    }

    void AttachmentTask::copyto(BehaviorTask* target) const
    {
        super::copyto(target);
    }

    void AttachmentTask::save(ISerializableNode* node) const
    {
        super::save(node);
    }

    void AttachmentTask::load(ISerializableNode* node)
    {
        super::load(node);
    }

    void LeafTask::copyto(BehaviorTask* target) const
    {
        super::copyto(target);
    }

    void LeafTask::save(ISerializableNode* node) const
    {
        super::save(node);
    }

    void LeafTask::load(ISerializableNode* node)
    {
        super::load(node);
    }

    struct getnode_t
    {
        int				id_;
        BehaviorTask*	task_;

        getnode_t(int id) : id_(id), task_(0)
        {}

        getnode_t& operator=(const getnode_t&);
    };

    bool getid_handler(BehaviorTask* task, Agent* pAgent, void* user_data)
    {
        BEHAVIAC_UNUSED_VAR(pAgent);
        getnode_t* temp = (getnode_t*)user_data;

        int oldId = task->GetId();

        if (oldId == temp->id_)
        {
            temp->task_ = task;

            return false;
        }

        return true;
    }

    void BranchTask::copyto(BehaviorTask* target) const
    {
        super::copyto(target);

        BEHAVIAC_ASSERT(BranchTask::DynamicCast(target));
        BranchTask* ttask = (BranchTask*)target;

        if (this->m_currentTask)
        {
            int id = this->m_currentTask->GetId();
            getnode_t temp(id);

            ttask->traverse(&getid_handler, 0, &temp);
            BEHAVIAC_ASSERT(temp.task_);

            ttask->m_currentTask = temp.task_;
        }
    }

    void BranchTask::save(ISerializableNode* node) const
    {
        super::save(node);

        if (this->m_status != BT_INVALID)
        {
            int id = -1;

            if (this->m_currentTask)
            {
                id = this->m_currentTask->GetId();
            }

            CSerializationID attrId("current");
            node->setAttr(attrId, id);
        }
    }

    void BranchTask::load(ISerializableNode* node)
    {
        super::load(node);

        if (this->m_status != BT_INVALID)
        {
            CSerializationID  attrId("current");
            behaviac::string attrStr;

            if (node->getAttr(attrId, attrStr))
            {
                int currentNodeId = -1;
                StringUtils::FromString(attrStr.c_str(), currentNodeId);

                if (currentNodeId != -1)
                {
                    this->m_currentTask = (BehaviorTask*)this->GetTaskById(currentNodeId);
                }
            }
        }
    }

    EBTStatus BranchTask::execCurrentTask(Agent* pAgent, EBTStatus childStatus)
    {
        BEHAVIAC_ASSERT(this->m_currentTask != 0 && this->m_currentTask->GetStatus() == BT_RUNNING);

        //this->m_currentTask could be cleared in ::tick, to remember it
		EBTStatus status = this->m_currentTask->exec(pAgent, childStatus);

        //give the handling back to parents
        if (status != BT_RUNNING)
        {
            BEHAVIAC_ASSERT(status == BT_SUCCESS || status == BT_FAILURE);
            BEHAVIAC_ASSERT(this->m_currentTask->m_status == status);

            BranchTask* parentBranch = this->m_currentTask->GetParent();

            this->m_currentTask = 0;

            //back track the parents until the branch
            while (parentBranch != 0)
            {
				if (parentBranch == this)
				{
					status = parentBranch->update(pAgent, status);
				}
				else
				{
					status = parentBranch->exec(pAgent, status);
				}

                if (status == BT_RUNNING)
                {
                    return BT_RUNNING;
                }

				BEHAVIAC_ASSERT(parentBranch == this || parentBranch->m_status == status);
				if (parentBranch == this)
				{
					break;
				}

                parentBranch = parentBranch->GetParent();
            }
        }

        return status;
    }

    EBTStatus BranchTask::resume_branch(Agent* pAgent, EBTStatus status)
    {
        BEHAVIAC_ASSERT(this->m_currentTask != 0);
        BEHAVIAC_ASSERT(status == BT_SUCCESS || status == BT_FAILURE);

        BranchTask* parent = 0;
        BehaviorNode* _tNode = (BehaviorNode*) this->m_currentTask->m_node;

        if (_tNode->IsManagingChildrenAsSubTrees())
        {
            parent = (BranchTask*)this->m_currentTask;
        }
        else
        {
            parent = this->m_currentTask->GetParent();
        }

        //clear it as it ends and the next exec might need to set it
        this->m_currentTask = 0;

        EBTStatus s = parent->exec(pAgent, status);

        return s;
    }

	EBTStatus BranchTask::update_current(Agent* pAgent, EBTStatus childStatus)
	{
		EBTStatus status = BT_INVALID;

		if (this->m_currentTask != 0)
		{
			status = this->execCurrentTask(pAgent, childStatus);
			BEHAVIAC_ASSERT(status == BT_RUNNING ||
				(status != BT_RUNNING && this->m_currentTask == 0));
		}
		else
		{
			status = this->update(pAgent, childStatus);
		}

		return status;
	}

    int CompositeTask::InvalidChildIndex = (int) - 1;

    CompositeTask::CompositeTask() : BranchTask(), m_activeChildIndex(InvalidChildIndex)
    {
    }

    void CompositeTask::Init(const BehaviorNode* node)
    {
        super::Init(node);
        BEHAVIAC_ASSERT(node->GetChildrenCount() > 0);

        uint32_t childrenCount = node->GetChildrenCount();

        for (uint32_t i = 0; i < childrenCount; i++)
        {
            const BehaviorNode* childNode = node->GetChild(i);
            BehaviorTask* childTask = childNode->CreateAndInitTask();

            this->addChild(childTask);
        }
    }

    void CompositeTask::copyto(BehaviorTask* target) const
    {
        super::copyto(target);

        BEHAVIAC_ASSERT(CompositeTask::DynamicCast(target));
        CompositeTask* ttask = (CompositeTask*)target;

        ttask->m_activeChildIndex = this->m_activeChildIndex;

        BEHAVIAC_ASSERT(this->m_children.size() > 0);
        BEHAVIAC_ASSERT(this->m_children.size() == ttask->m_children.size());

        BehaviorTasks_t::size_type count = this->m_children.size();

        for (BehaviorTasks_t::size_type i = 0; i < count; ++i)
        {
            BehaviorTask* childTask = this->m_children[i];
            BehaviorTask* childTTask = ttask->m_children[i];

            childTask->copyto(childTTask);
        }
    }

    void CompositeTask::save(ISerializableNode* node) const
    {
        super::save(node);

        if (this->m_status != BT_INVALID)
        {
            CSerializationID attrId("activeChildIndex");
            node->setAttr(attrId, this->m_activeChildIndex);

            BehaviorTasks_t::size_type count = this->m_children.size();

            for (BehaviorTasks_t::size_type i = 0; i < count; ++i)
            {
                BehaviorTask* childTask = this->m_children[i];

                CSerializationID  nodeId("node");
                ISerializableNode* chidlNode = node->newChild(nodeId);
                childTask->save(chidlNode);
            }
        }
    }

    void CompositeTask::load(ISerializableNode* node)
    {
        super::load(node);

        if (this->m_status != BT_INVALID)
        {
            CSerializationID attrId("activeChildIndex");
            behaviac::string attrStr;
            node->getAttr(attrId, attrStr);
            StringUtils::FromString(attrStr.c_str(), this->m_activeChildIndex);

            //#if !BEHAVIAC_RELEASE
            //			if (this->m_activeChildIndex != uint32_t(-1))
            //			{
            //				BEHAVIAC_ASSERT(this->m_currentTask == this->m_children[this->m_activeChildIndex]);
            //			}
            //			else
            //			{
            //				BEHAVIAC_ASSERT(this->m_currentTask == 0);
            //			}
            //#endif

            BehaviorTasks_t::size_type count = this->m_children.size();
            BEHAVIAC_ASSERT(count == (BehaviorTasks_t::size_type)node->getChildCount());

            for (BehaviorTasks_t::size_type i = 0; i < count; ++i)
            {
                BehaviorTask* childTask = this->m_children[i];

                //CSerializationID  nodeId("node");
                ISerializableNode* chidlNode = node->getChild(i);
                childTask->load(chidlNode);
            }
        }
    }

    CompositeTask::~CompositeTask()
    {
        for (size_t i = 0; i < this->m_children.size(); ++i)
        {
            BehaviorTask* pChild = this->m_children[i];
            BEHAVIAC_DELETE(pChild);
        }

        this->m_children.clear();
    }

	BehaviorTask* CompositeTask::GetChildById(int nodeId) const
	{
		if (this->m_children.size() > 0)
		{
			for (unsigned int i = 0; i < this->m_children.size(); ++i)
			{
				BehaviorTask* c = this->m_children[i];

				if (c->GetId() == nodeId)
				{
					return c;
				}
			}
		}

		return 0;
	}

    const BehaviorTask* CompositeTask::GetTaskById(int id) const
    {
        BEHAVIAC_ASSERT(id != -1);

        const BehaviorTask* t = super::GetTaskById(id);

        if (t)
        {
            return t;
        }

        for (size_t i = 0; i < this->m_children.size(); ++i)
        {
            const BehaviorTask* pChild = this->m_children[i];
            const BehaviorTask* t1 = pChild->GetTaskById(id);

            if (t1)
            {
                return t1;
            }
        }

        return 0;
    }

    void CompositeTask::addChild(BehaviorTask* pBehavior)
    {
        pBehavior->SetParent(this);

        this->m_children.push_back(pBehavior);
    }

    void CompositeTask::traverse(NodeHandler_t handler, Agent* pAgent, void* user_data)
    {
        if (handler(this, pAgent, user_data))
        {
            for (BehaviorTasks_t::iterator it = this->m_children.begin();
                 it != this->m_children.end(); ++it)
            {
                //BehaviorTask* task = *it;
                (*it)->traverse(handler, pAgent, user_data);
                //task->traverse(handler, pAgent, user_data);
            }
        }
    }

    SingeChildTask::SingeChildTask() : m_root(0)
    {}

    SingeChildTask::~SingeChildTask()
    {
        BEHAVIAC_DELETE(m_root);
    }

    void SingeChildTask::addChild(BehaviorTask* pBehavior)
    {
        pBehavior->SetParent(this);

        this->m_root = pBehavior;
    }

    void SingeChildTask::traverse(NodeHandler_t handler, Agent* pAgent, void* user_data)
    {
        if (handler(this, pAgent, user_data))
        {
            if (this->m_root)
            {
                this->m_root->traverse(handler, pAgent, user_data);
            }
        }
    }

    void SingeChildTask::Init(const BehaviorNode* node)
    {
        super::Init(node);

        BEHAVIAC_ASSERT(node->GetChildrenCount() <= 1);

        if (node->GetChildrenCount() == 1)
        {
            const BehaviorNode* childNode = node->GetChild(0);

            BehaviorTask* childTask = childNode->CreateAndInitTask();

            this->addChild(childTask);

        }
        else
        {
            BEHAVIAC_ASSERT(true);
        }
    }

    void SingeChildTask::copyto(BehaviorTask* target) const
    {
        super::copyto(target);

        BEHAVIAC_ASSERT(SingeChildTask::DynamicCast(target));
        SingeChildTask* ttask = (SingeChildTask*)target;

        if (this->m_root)
        {
            //referencebehavior/query, etc.
            if (!ttask->m_root)
            {
                const BehaviorNode* pNode = this->m_root->GetNode();
                BEHAVIAC_ASSERT(BehaviorTree::DynamicCast(pNode));
                ttask->m_root = pNode->CreateAndInitTask();
            }

            BEHAVIAC_ASSERT(ttask->m_root);
            this->m_root->copyto(ttask->m_root);
        }
    }

    void SingeChildTask::save(ISerializableNode* node) const
    {
        super::save(node);

        if (this->m_status != BT_INVALID)
        {
            if (this->m_root)
            {
                CSerializationID  nodeId("root");
                ISerializableNode* chidlNode = node->newChild(nodeId);
                this->m_root->save(chidlNode);
            }
        }
    }

    void SingeChildTask::load(ISerializableNode* node)
    {
        super::load(node);

        if (this->m_status != BT_INVALID)
        {
            CSerializationID  rootId("root");
            ISerializableNode* rootNode = node->findChild(rootId);
            BEHAVIAC_ASSERT(rootNode);
            this->m_root->load(rootNode);
        }
    }

    const BehaviorTask* SingeChildTask::GetTaskById(int id) const
    {
        BEHAVIAC_ASSERT(id != -1);
        const BehaviorTask* t = super::GetTaskById(id);

        if (t)
        {
            return t;
        }

        if (this->m_root->GetId() == id)
        {
            return this->m_root;
        }

        return this->m_root->GetTaskById(id);
    }

    EBTStatus SingeChildTask::update(Agent* pAgent, EBTStatus childStatus)
    {
        BEHAVIAC_UNUSED_VAR(childStatus);
        if (this->m_root)
        {
            EBTStatus s = this->m_root->exec(pAgent);
            return s;
        }

		return BT_FAILURE;
    }

    DecoratorTask::DecoratorTask() : SingeChildTask(), m_bDecorateWhenChildEnds(false)
    {
    }

    void DecoratorTask::Init(const BehaviorNode* node)
    {
        super::Init(node);
        DecoratorNode* pDN = (DecoratorNode*)node;

        this->m_bDecorateWhenChildEnds = pDN->m_bDecorateWhenChildEnds;
    }

    void DecoratorTask::copyto(BehaviorTask* target) const
    {
        super::copyto(target);

        // BEHAVIAC_ASSERT(DecoratorTask::DynamicCast(target));
        // DecoratorTask* ttask = (DecoratorTask*)target;
    }

    void DecoratorTask::save(ISerializableNode* node) const
    {
        super::save(node);
    }

    void DecoratorTask::load(ISerializableNode* node)
    {
        super::load(node);
    }

    DecoratorTask::~DecoratorTask()
    {
    }

    bool DecoratorTask::onenter(Agent* pAgent)
    {
        BEHAVIAC_UNUSED_VAR(pAgent);

        return true;
    }

	EBTStatus DecoratorTask::update_current(Agent* pAgent, EBTStatus childStatus)
	{
		return super::update_current(pAgent, childStatus);
	}

    EBTStatus DecoratorTask::update(Agent* pAgent, EBTStatus childStatus)
    {
        BEHAVIAC_ASSERT(DecoratorNode::DynamicCast(this->m_node) != 0);
        DecoratorNode* node = (DecoratorNode*)this->m_node;

        EBTStatus status = BT_INVALID;

        if (childStatus != BT_RUNNING)
        {
            status = childStatus;

            if (!node->m_bDecorateWhenChildEnds || status != BT_RUNNING)
            {
                EBTStatus result = this->decorate(status);

                if (result != BT_RUNNING)
                {
                    return result;
                }

                return BT_RUNNING;
            }
        }

        status = super::update(pAgent, childStatus);

        if (!node->m_bDecorateWhenChildEnds || status != BT_RUNNING)
        {
            EBTStatus result = this->decorate(status);

            return result;
        }

        return BT_RUNNING;
    }

    LeafTask::LeafTask() : BehaviorTask()
    {}

    void LeafTask::Init(const BehaviorNode* node)
    {
        super::Init(node);

        //BEHAVIAC_ASSERT(node && node->GetChildrenCount() == 0);
    }

    LeafTask::~LeafTask()
    {}

    void LeafTask::traverse(NodeHandler_t handler, Agent* pAgent, void* user_data)
    {
        handler(this, pAgent, user_data);
    }

    BehaviorTreeTask::BehaviorTreeTask() : SingeChildTask()
    {}

    void BehaviorTreeTask::Init(const BehaviorNode* node)
    {
        BEHAVIAC_ASSERT(node != 0);
        // BehaviorTree* tree = (BehaviorTree*)node;

		super::Init(node);
    }

    void BehaviorTreeTask::copyto(BehaviorTask* target) const
    {
        super::copyto(target);

        // BEHAVIAC_ASSERT(BehaviorTreeTask::DynamicCast(target));
        // BehaviorTreeTask* ttask = (BehaviorTreeTask*)target;
    }

    void BehaviorTreeTask::save(ISerializableNode* node) const
    {
        super::save(node);
    }

    void BehaviorTreeTask::load(ISerializableNode* node)
    {
        super::load(node);
    }

    BehaviorTreeTask::~BehaviorTreeTask()
    {
        //if (this->m_root)
        //{
        //	BehaviorTask::DestroyTask(this->m_root);
        //}
    }

    void BehaviorTreeTask::Clear()
    {
        BehaviorTask::Clear();

        BEHAVIAC_DELETE this->m_root;
        this->m_root = 0;

        this->m_currentTask = 0;
    }

    const behaviac::string& BehaviorTreeTask::GetName() const
    {
        BEHAVIAC_ASSERT(BehaviorTree::DynamicCast(this->m_node));
        const BehaviorTree* bt = (const BehaviorTree*)(this->m_node);
        BEHAVIAC_ASSERT(bt);
        return bt->GetName();
    }

    bool BehaviorTreeTask::onenter(Agent* pAgent)
    {
        pAgent->LogJumpTree(this->GetName());

        return true;
    }

    void BehaviorTreeTask::onexit(Agent* pAgent, EBTStatus status)
    {
        pAgent->LogReturnTree(this->GetName());
        super::onexit(pAgent, status);
    }

	EBTStatus BehaviorTreeTask::update_current(Agent* pAgent, EBTStatus childStatus)
	{
		BEHAVIAC_ASSERT(this->m_node != 0);
		BEHAVIAC_ASSERT(BehaviorTree::DynamicCast(this->m_node) != 0);
		BehaviorTree* tree = (BehaviorTree*)this->m_node;

		EBTStatus status = BT_RUNNING;

		if (tree->IsFSM())
		{
			status = this->update(pAgent, childStatus);
		}
		else
		{
			status = super::update_current(pAgent, childStatus);
		}

		return status;
	}


    EBTStatus BehaviorTreeTask::update(Agent* pAgent, EBTStatus childStatus)
    {
        BEHAVIAC_ASSERT(this->m_node != 0);
        BEHAVIAC_ASSERT(this->m_root != 0);

        if (childStatus != BT_RUNNING)
        {
            return childStatus;
        }

        EBTStatus status = BT_INVALID;

        status = super::update(pAgent, childStatus);

        BEHAVIAC_ASSERT(status != BT_INVALID);

        return status;
    }

    void BehaviorTreeTask::SetRootTask(BehaviorTask* pRoot)
    {
        this->addChild(pRoot);
    }

    void BehaviorTreeTask::CopyTo(BehaviorTreeTask* target)
    {
        this->copyto(target);
    }

    EBTStatus BehaviorTreeTask::resume(Agent* pAgent, EBTStatus status)
    {
        EBTStatus s = super::resume_branch(pAgent, status);

        return s;
    }

    bool BehaviorTask::onevent(Agent* pAgent, const char* eventName)
    {
        if (this->m_status == BT_RUNNING && this->m_node->m_bHasEvents)
        {
            if (!this->CheckEvents(eventName, pAgent))
            {
                return false;
            }
        }

        return true;
    }

    void BranchTask::Init(const BehaviorNode* node)
    {
        super::Init(node);
    }

    bool BranchTask::oneventCurrentNode(Agent* pAgent, const char* eventName)
    {
        BEHAVIAC_ASSERT(this->m_currentTask);

        EBTStatus s = this->m_currentTask->GetStatus();
        BEHAVIAC_UNUSED_VAR(s);

        BEHAVIAC_ASSERT(s == BT_RUNNING && this->m_node->HasEvents());

        bool bGoOn = this->m_currentTask->onevent(pAgent, eventName);

        //give the handling back to parents
        if (bGoOn)
        {
            BranchTask* parentBranch = this->m_currentTask->GetParent();

            //back track the parents until the branch
            while (parentBranch && parentBranch != this)
            {
                BEHAVIAC_ASSERT(parentBranch->GetStatus() == BT_RUNNING);

                bGoOn = parentBranch->onevent(pAgent, eventName);

                if (!bGoOn)
                {
                    return false;
                }

                parentBranch = parentBranch->GetParent();
            }
        }

        return bGoOn;
    }

    bool BranchTask::onevent(Agent* pAgent, const char* eventName)
    {
        if (this->m_node->HasEvents())
        {
	        bool bGoOn = true;

            if (this->m_currentTask)
            {
                bGoOn = this->oneventCurrentNode(pAgent, eventName);
            }

			if (bGoOn)
			{
				bGoOn = super::onevent(pAgent, eventName);
			}
        }

        return true;
    }

    bool LeafTask::onevent(Agent* pAgent, const char* eventName)
    {
        bool bGoOn = super::onevent(pAgent, eventName);

        return bGoOn;
    }

    bool BehaviorTreeTask::onevent(Agent* pAgent, const char* eventName)
    {
		return super::onevent(pAgent, eventName);
    }

    void BehaviorTreeTask::Save(ISerializableNode* node) const
    {
        CSerializationID  btId("BehaviorTree");
        ISerializableNode* btNodeRoot = node->newChild(btId);

        BEHAVIAC_ASSERT(BehaviorTree::DynamicCast(this->GetNode()));
        BehaviorTree* bt = (BehaviorTree*)this->GetNode();

        CSerializationID  sourceId("source");
        btNodeRoot->setAttr(sourceId, bt->GetName());

        CSerializationID  nodeId("node");
        ISerializableNode* btNode = btNodeRoot->newChild(nodeId);

        this->save(btNode);
    }

    void BehaviorTreeTask::Load(ISerializableNode* node)
    {
        this->load(node);
    }
}//namespace behaviac
