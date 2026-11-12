// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Included inside FLspStdioSession. Protocol state stays on the ordered worker;
// the analysis worker receives owned, immutable document/catalog snapshots.

			struct FReadySemanticTokens
			{
				std::uint64_t Revision = 0;
				std::shared_ptr<const FJson::FArray> Data;
				std::vector<std::string> Dependencies;
				std::shared_ptr<const Detail::FSemanticQuerySnapshot> Snapshot;
			};

			struct FSemanticWorkspaceSnapshot
			{
				VeroScript::FHostBindingRegistry Catalog;
				VeroScriptWorkspace Workspace;
			};

			struct FSemanticAnalysisJob
			{
				FJson Id;
				FJson Params;
				std::string Path;
				std::string Source;
				std::uint64_t Revision = 0;
				std::uint64_t Configuration = 0;
				std::shared_ptr<std::atomic<std::uint64_t>> DocumentRevision;
				std::uint64_t DocumentVersion = 0;
				std::shared_ptr<const FSemanticWorkspaceSnapshot> Snapshot;
				bool bDelta = false;
				bool bTrace = false;
				bool bCancelled = false;
				bool bCacheHit = false;
				std::string Error;
				std::shared_ptr<const FJson::FArray> Data;
				std::vector<std::string> Dependencies;
				std::shared_ptr<const Detail::FSemanticQuerySnapshot> QuerySnapshot;
				bool bSuccess = false;
				std::map<std::string, double> Stages;
				std::chrono::steady_clock::time_point Queued;
			};

			static bool MayAffectSemanticDependencies(std::string_view Path,
				const std::vector<std::string>& Dependencies)
			{
				const std::string Key = PathUtil::IdentityKey(std::string(Path));
				const std::string Name = Key.substr(Key.find_last_of('/') + 1);
				return std::any_of(Dependencies.begin(), Dependencies.end(),
					[&](const std::string& Dependency)
					{
						const std::string DependencyKey = PathUtil::IdentityKey(Dependency);
						// A new overlay with the same filename can change import-search
						// precedence. Invalidate conservatively even outside the old path.
						return Key == DependencyKey || Name == DependencyKey.substr(
								DependencyKey.find_last_of('/') + 1);
					});
			}

			void InvalidateSemanticAnalysis(std::string_view Path = {})
			{
				++SemanticRevision;
				if (Path.empty())
				{
					++SemanticConfiguration;
					ReadySemanticTokens.clear();
				}
				else
				{
					const std::string Key = PathUtil::IdentityKey(std::string(Path));
					auto& Revision = SemanticDocumentRevisions[Key];
					if (!Revision)
					{
						Revision = std::make_shared<std::atomic<std::uint64_t>>(0);
					}
					++*Revision;
					for (auto It = ReadySemanticTokens.begin(); It != ReadySemanticTokens.end();)
					{
						if (It->first == Key || MayAffectSemanticDependencies(Path, It->second.Dependencies))
						{
							It = ReadySemanticTokens.erase(It);
						}
						else
						{
							It->second.Revision = SemanticRevision.load();
							++It;
						}
					}
				}
				// Keep the last successfully published integer stream for delta requests.
			}

			void QueueSemanticTokens(const FJson& Id, const FJson& Params, bool bDelta)
			{
				const FJson* Document = Params.Find("textDocument");
				if (!Document)
				{
					WriteResponseResult(Id, FJson::Object({{bDelta ? "edits" : "data", FJson::Array()}}));
					return;
				}
				const std::string Path = UriToPath(Document->GetString("uri"));
				const std::string Key = PathUtil::IdentityKey(Path);
				const std::uint64_t Revision = SemanticRevision.load();
				if (const auto Ready = ReadySemanticTokens.find(Key);
					Ready != ReadySemanticTokens.end() && Ready->second.Revision == Revision
					&& Detail::IsSemanticQuerySnapshotCacheable(Ready->second.Snapshot)
					&& Detail::IsSemanticQuerySnapshotCurrent(Ready->second.Snapshot))
				{
					if (bTraceAnalysis)
					{
						WriteNotification("window/logMessage", FJson::Object({{"type", FJson::Number(4)}, {"message", FJson::String("semanticTokens " + Path + " cache=hit")}}));
					}
					ReplySemanticTokens(Id, Params, *Ready->second.Data, bDelta);
					return;
				}
				bool bReserved = false;
				{
					std::lock_guard<std::mutex> Lock(IncomingMutex);
					if (PendingSemanticJobs < 64)
					{
						++PendingSemanticJobs;
						bReserved = true;
					}
				}
				if (!bReserved)
				{
					WriteResponseError(Id, -32802, Locale::TranslateId("lsp.semantic_queue_full"));
					return;
				}
				struct FQueueReservation
				{
					~FQueueReservation()
					{
						if (!bCommitted)
						{
							std::lock_guard<std::mutex> Lock(Session.IncomingMutex);
							--Session.PendingSemanticJobs;
							Session.IncomingCondition.notify_all();
						}
					}

					FLspStdioSession& Session;

					bool bCommitted = false;
				} Reservation{*this};
				auto Job = std::make_shared<FSemanticAnalysisJob>();
				Job->Id = Id;
				Job->Params = Params;
				Job->Path = Path;
				Job->Source = SourceForPath(Path);
				Job->Revision = Revision;
				Job->Configuration = SemanticConfiguration.load();
				auto& DocumentRevision = SemanticDocumentRevisions[Key];
				if (!DocumentRevision)
				{
					DocumentRevision = std::make_shared<std::atomic<std::uint64_t>>(0);
				}
				Job->DocumentRevision = DocumentRevision;
				Job->DocumentVersion = DocumentRevision->load();
				if (QueuedSnapshotRevision == Revision)
				{
					Job->Snapshot = QueuedSnapshot.lock();
				}
				if (!Job->Snapshot)
				{
					// Requests for the same immutable revision can share all open
					// buffers/catalogs. Previously every queued request copied them.
					auto Snapshot = std::make_shared<FSemanticWorkspaceSnapshot>();
					Snapshot->Catalog = BindingCatalog;
					Snapshot->Workspace = Workspace;
					FWorkspaceOptions Options = Snapshot->Workspace.GetOptions();
					Options.Bindings = Workspace.GetOptions().Bindings ? &Snapshot->Catalog : nullptr;
					Snapshot->Workspace.SetOptions(Options);
					Job->Snapshot = std::move(Snapshot);
					QueuedSnapshot = Job->Snapshot;
					QueuedSnapshotRevision = Revision;
				}
				CheckAnalysisCancellation();
				Job->bDelta = bDelta;
				Job->bTrace = bTraceAnalysis;
				Job->Queued = std::chrono::steady_clock::now();
				{
					std::lock_guard<std::mutex> Lock(IncomingMutex);
					SemanticJobs.push_back(std::move(Job));
					Reservation.bCommitted = true;
				}
				IncomingCondition.notify_all();
			}

			void RunSemanticWorker()
			{
				// One worker owns this cache. It also coalesces identical requests that
				// arrived before the protocol worker published the first result.
				std::map<std::string, FReadySemanticTokens> Cache;
				std::uint64_t CacheRevision = 0;
				for (;;)
				{
					std::shared_ptr<FSemanticAnalysisJob> Job;
					{
						std::unique_lock<std::mutex> Lock(IncomingMutex);
						IncomingCondition.wait(Lock, [this]()
						{
							return bStopSemanticWorker || !SemanticJobs.empty();
						});
						if (SemanticJobs.empty())
						{
							return;
						}
						Job = std::move(SemanticJobs.front());
						SemanticJobs.pop_front();
					}
					const std::string Key = PathUtil::IdentityKey(Job->Path);
					if (CacheRevision != Job->Revision)
					{
						Cache.clear();
						CacheRevision = Job->Revision;
					}
					const auto ShouldCancel = [this, Job]()
					{
						return Job->Configuration != SemanticConfiguration.load()
							|| Job->DocumentVersion != Job->DocumentRevision->load()
							|| IsRequestCancellationRequested(Job->Id);
					};
					std::function<void(std::string_view, double)> RecordStage;
					if (Job->bTrace)
					{
						Job->Stages["queue"] = std::chrono::duration<double, std::milli>(
							std::chrono::steady_clock::now() - Job->Queued)
												   .count();
						RecordStage = [Job](std::string_view Name, double Milliseconds)
						{
							Job->Stages[std::string(Name)] += Milliseconds;
						};
					}
					try
					{
						FAnalysisControlScope Control(ShouldCancel, RecordStage);
						CheckAnalysisCancellation();
						if (const auto Ready = Cache.find(Key); Ready != Cache.end()
							&& Detail::IsSemanticQuerySnapshotCacheable(Ready->second.Snapshot)
							&& Detail::IsSemanticQuerySnapshotCurrent(Ready->second.Snapshot))
						{
							Job->Data = Ready->second.Data;
							Job->Dependencies = Ready->second.Dependencies;
							Job->QuerySnapshot = Ready->second.Snapshot;
							Job->bSuccess = true;
							Job->bCacheHit = true;
						}
						else
						{
							FSemanticTokensOptions QueryOptions;
							QueryOptions.ShouldCancel = ShouldCancel;
							QueryOptions.RecordStage = RecordStage;
							const FSemanticTokensResult Result =
									Job->Snapshot->Workspace.SemanticTokensDocument(Job->Path, QueryOptions);
							if (Result.bCancelled)
							{
								throw FAnalysisCancelled();
							}
							Job->bSuccess = Result.bSuccess;
							Job->QuerySnapshot = Result.Snapshot;
							Job->Dependencies = Result.Dependencies;
							Job->Data = std::make_shared<const FJson::FArray>(
								BuildSemanticTokenData(Job->Source, Result));
							CheckAnalysisCancellation();
							if (Cache.size() >= 64)
							{
								Cache.clear();
							}
							if (Job->bSuccess && Detail::IsSemanticQuerySnapshotCacheable(Job->QuerySnapshot))
							{
								Cache[Key] = {Job->Revision, Job->Data, Job->Dependencies, Job->QuerySnapshot};
							}
						}
					}
					catch (const FAnalysisCancelled&)
					{
						Job->bCancelled = true;
					}
					catch (const std::exception& Error)
					{
						Job->Error = Error.what();
					}
					{
						std::lock_guard<std::mutex> Lock(IncomingMutex);
						CompletedSemanticJobs.push_back(std::move(Job));
					}
					IncomingCondition.notify_all();
				}
			}

			// Called only while IncomingMutex is held. Expired queued work must reply
			// without waiting for another document's analysis to finish.
			void ExpireQueuedSemanticJobs()
			{
				const auto Now = std::chrono::steady_clock::now();
				for (auto It = SemanticJobs.begin(); It != SemanticJobs.end();)
				{
					const auto& Job = *It;
					const std::string Key = ToJsonString(Job->Id);
					const auto Deadline = ActiveRequestDeadlines.find(Key);
					if (CancelledRequestIds.count(Key) || (Deadline != ActiveRequestDeadlines.end()
						&& Now >= Deadline->second) || Job->Configuration != SemanticConfiguration.load()
						|| Job->DocumentVersion != Job->DocumentRevision->load())
					{
						Job->bCancelled = true;
						CompletedSemanticJobs.push_back(std::move(*It));
						It = SemanticJobs.erase(It);
					}
					else
					{
						++It;
					}
				}
			}

			void CompleteSemanticJob(const FSemanticAnalysisJob& Job)
			{
				if (IsRequestCancellationRequested(Job.Id))
				{
					WriteResponseResult(Job.Id, FJson::Null());
					return;
				}
				bool bStale = Job.Configuration != SemanticConfiguration.load()
					|| Job.DocumentVersion != Job.DocumentRevision->load();
				if (!bStale && Job.Revision != SemanticRevision.load())
				{
					// An unrelated document update must not discard this request. Compare
					// relevant overlays, including possible import-precedence changes.
					const auto Previous = Job.Snapshot->Workspace.SnapshotDocuments();
					const auto Current = Workspace.SnapshotDocuments();
					const auto CheckChangedOverlays = [&](const auto& Documents, const auto& Other)
					{
						return std::any_of(Documents.begin(), Documents.end(), [&](const FDocumentOverlay& Document)
						{
							if (!MayAffectSemanticDependencies(Document.Path, Job.Dependencies))
							{
								return false;
							}
							const auto Before = std::find_if(Other.begin(), Other.end(), [&](const FDocumentOverlay& Candidate)
							{
								return PathUtil::Equal(Document.Path, Candidate.Path);
							});
							return Before == Other.end() || Before->Source != Document.Source;
						});
					};
					bStale |= CheckChangedOverlays(Current, Previous) || CheckChangedOverlays(Previous, Current);
				}
				bStale |= Job.QuerySnapshot && !Detail::IsSemanticQuerySnapshotCurrent(Job.QuerySnapshot);
				if (bStale)
				{
					WriteResponseError(Job.Id, -32801, Locale::TranslateId("lsp.content_modified"));
					return;
				}
				if (!Job.Error.empty() || Job.bCancelled || !Job.Data)
				{
					WriteResponseError(Job.Id, -32603,
						Job.Error.empty() ? Locale::TranslateId("lsp.semantic_analysis_stopped") : Job.Error);
					return;
				}
				if (Job.bSuccess && Detail::IsSemanticQuerySnapshotCacheable(Job.QuerySnapshot))
				{
					if (ReadySemanticTokens.size() >= 64)
					{
						ReadySemanticTokens.clear();
					}
					ReadySemanticTokens[PathUtil::IdentityKey(Job.Path)] = {
						SemanticRevision.load(), Job.Data, Job.Dependencies, Job.QuerySnapshot};
				}
				if (Job.bTrace)
				{
					std::string Message = "semanticTokens " + Job.Path;
					for (const auto& Stage : Job.Stages)
					{
						Message += " " + Stage.first + "=" + std::to_string(Stage.second) + "ms";
					}
					Message += Job.bCacheHit ? " cache=hit" : " cache=miss";
					WriteNotification("window/logMessage", FJson::Object({{"type", FJson::Number(4)}, {"message", FJson::String(Message)}}));
				}
				ReplySemanticTokens(Job.Id, Job.Params, *Job.Data, Job.bDelta);
			}
